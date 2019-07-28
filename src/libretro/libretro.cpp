#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <stdio.h>

#if defined(_WIN32) && !defined(_XBOX)
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#define socket_t    SOCKET
#define sockaddr_t  SOCKADDR
#define pcap_dev_name description
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#define socket_t    int
#define sockaddr_t  struct sockaddr
#define closesocket close
#define pcap_dev_name name
#endif

#if defined(__HAIKU__)
#include <posix/sys/select.h>
#endif
#if defined(__SWITCH__)
#include <sys/select.h>
#endif

#ifdef HAVE_PCAP
#include "libui_sdl/LAN_PCap.h"
#include "libui_sdl/LAN_Socket.h"
#endif

#include "version.h"
#include "Config.h"
#include "Platform.h"
#include "NDS.h"
#include "GPU.h"
#include "SPU.h"
#include "libretro.h"
#include <streams/file_stream.h>
#include <streams/memory_stream.h>
#include <file/file_path.h>

#include "screenlayout.h"

#ifndef INVALID_SOCKET
#define INVALID_SOCKET  (socket_t)-1
#endif

socket_t MPSocket;
sockaddr_t MPSendAddr;
u8 PacketBuffer[2048];

static bool initialized_opengl = false; // if the opengl context has initialized
static bool using_opengl = false; // if the opengl renderer is currently used
static bool update_opengl = true; // update the ubo / vao

enum CurrentRenderer
{
   None,
   Software,
   OpenGL,
};

static CurrentRenderer current_renderer = CurrentRenderer::None;

#ifdef HAVE_OPENGL
#include <glsm/glsmsym.h>
struct retro_hw_render_callback hw_render;

#include "OpenGLSupport.h"
#include "shaders.h"
static GLuint shader[3];
static GLuint screen_framebuffer_texture;
static float screen_vertices[2 * 3 * 2 * 4];
static GLuint vao, vbo;

struct
{
   GLfloat uScreenSize[2];
   u32 u3DScale;
   u32 uFilterMode;
   GLint cursorPos[4];

} GL_ShaderConfig;
GLuint ubo;

#endif

#define CURSOR_SIZE 2 // TODO: Maybe make this adjustable

#define NIFI_VER 1

#ifdef HAVE_THREADS
/*  Code taken from http://greenteapress.com/semaphores/semaphore.c
 *  and changed to use libretro-common's mutexes and conditions.
 */

#include <stdlib.h>

#include <rthreads/rthreads.h>

typedef struct ssem ssem_t;

struct ssem
{
   int value;
   int wakeups;
   slock_t *mutex;
   scond_t *cond;
};

ssem_t *ssem_new(int value)
{
   ssem_t *semaphore = (ssem_t*)calloc(1, sizeof(*semaphore));

   if (!semaphore)
      goto error;

   semaphore->value   = value;
   semaphore->wakeups = 0;
   semaphore->mutex   = slock_new();

   if (!semaphore->mutex)
      goto error;

   semaphore->cond = scond_new();

   if (!semaphore->cond)
      goto error;

   return semaphore;

error:
   if (semaphore->mutex)
      slock_free(semaphore->mutex);
   semaphore->mutex = NULL;
   if (semaphore)
      free((void*)semaphore);
   return NULL;
}

void ssem_free(ssem_t *semaphore)
{
   if (!semaphore)
      return;

   scond_free(semaphore->cond);
   slock_free(semaphore->mutex);
   free((void*)semaphore);
}

void ssem_wait(ssem_t *semaphore)
{
   if (!semaphore)
      return;

   slock_lock(semaphore->mutex);
   semaphore->value--;

   if (semaphore->value < 0)
   {
      do
      {
         scond_wait(semaphore->cond, semaphore->mutex);
      }while (semaphore->wakeups < 1);

      semaphore->wakeups--;
   }

   slock_unlock(semaphore->mutex);
}

void ssem_signal(ssem_t *semaphore)
{
   if (!semaphore)
      return;

   slock_lock(semaphore->mutex);
   semaphore->value++;

   if (semaphore->value <= 0)
   {
      semaphore->wakeups++;
      scond_signal(semaphore->cond);
   }

   slock_unlock(semaphore->mutex);
}
#endif

static inline int32_t Clamp(int32_t value, int32_t min, int32_t max)
{
   return std::max(min, std::min(max, value));
}

static bool touching;
static int32_t touch_x;
static int32_t touch_y;

enum TouchMode
{
   Disabled,
   Mouse,
   Touch,
};

static TouchMode current_touch_mode = TouchMode::Disabled;

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;
char retro_base_directory[4096];
char retro_saves_directory[4096];
bool retro_firmware_status;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}


static retro_environment_t environ_cb;

void retro_init(void)
{
   const char *dir = NULL;

   srand(time(NULL));
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
      sprintf(retro_base_directory, "%s", dir);

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
      sprintf(retro_saves_directory, "%s", dir);

   screen_layout_data.buffer_ptr = nullptr;
}

void retro_deinit(void)
{
   return;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "melonDS";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = MELONDS_VERSION GIT_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = "nds";
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;


namespace Platform
{
FILE* OpenFile(const char* path, const char* mode, bool mustexist)
{
    FILE* ret;

    if (mustexist)
    {
        ret = fopen(path, "rb");
        if (ret) ret = freopen(path, mode, ret);
    }
    else
        ret = fopen(path, mode);

    return ret;
}

   FILE* OpenLocalFile(const char* path, const char* mode)
   {
      bool relpath = false;
      int pathlen = strlen(path);

   #ifdef __WIN32__
      if (pathlen > 3)
      {
         if (path[1] == ':' && path[2] == '\\')
               return OpenFile(path, mode);
      }
   #else
      if (pathlen > 1)
      {
         if (path[0] == '/')
               return OpenFile(path, mode);
      }
   #endif

      if (pathlen >= 3)
      {
         if (path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\\'))
               relpath = true;
      }

      int emudirlen = strlen(retro_base_directory);
      char* emudirpath;
      if (emudirlen)
      {
         int len = emudirlen + 1 + pathlen + 1;
         emudirpath = new char[len];
         strncpy(&emudirpath[0], retro_base_directory, emudirlen - 1);
         emudirpath[emudirlen] = '/';
         strncpy(&emudirpath[emudirlen+1], path, pathlen - 1);
         emudirpath[emudirlen+1+pathlen] = '\0';
      }
      else
      {
         emudirpath = new char[pathlen+1];
         strncpy(&emudirpath[0], path, pathlen - 1);
         emudirpath[pathlen] = '\0';
      }

      // Locations are application directory, and AppData/melonDS on Windows or XDG_CONFIG_HOME/melonds on Linux

      FILE* f;

      // First check current working directory
      f = OpenFile(path, mode, true);
      if (f) { delete[] emudirpath; return f; }

      // then emu directory
      f = OpenFile(emudirpath, mode, true);
      if (f) { delete[] emudirpath; return f; }

      if (!relpath)
      {
         std::string fullpath = std::string(retro_base_directory) + "/melonds/" + path;
         f = OpenFile(fullpath.c_str(), mode, true);
         if (f) { delete[] emudirpath; return f; }
      }

      if (mode[0] != 'r')
      {
         f = OpenFile(emudirpath, mode);
         if (f) { delete[] emudirpath; return f; }
      }

      delete[] emudirpath;
      return NULL;
   }

   void StopEmu()
   {
       return;
   }

   void Semaphore_Reset(void *sema)
   {
      /* TODO/FIXME */
   }

   void Semaphore_Post(void *sema)
   {
   #if 0
   #ifdef HAVE_THREADS
      ssem_signal((ssem_t*)sema);
   #endif
   #endif
   }

   void Semaphore_Wait(void *sema)
   {
   #if 0
   #ifdef HAVE_THREADS
      ssem_wait((ssem_t*)sema);
   #endif
   #endif
   }

   void Semaphore_Free(void *sema)
   {
   #if 0
   #ifdef HAVE_THREADS
      ssem_t *sem = (ssem_t*)sema;
      if (sem)
         ssem_free(sem);
   #endif
   #endif
   }

   void *Semaphore_Create()
   {
   #if 0
   #ifdef HAVE_THREADS
      ssem_t *sem = ssem_new(0);
      if (sem)
         return sem;
   #endif
   #endif
      return NULL;
   }

   void Thread_Free(void *thread)
   {
      /* TODO/FIXME */
   }

   void *Thread_Create(void (*func)())
   {
      /* TODO/FIXME */
      return NULL;
   }

   void Thread_Wait(void *thread)
   {
      /* TODO/FIXME */
   }


   bool MP_Init()
   {
      int opt_true = 1;
      int res;

#ifdef _WIN32
      WSADATA wsadata;
      if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
      {
         return false;
      }
#endif // __WXMSW__

      MPSocket = socket(AF_INET, SOCK_DGRAM, 0);
      if (MPSocket < 0)
      {
         return false;
      }

      res = setsockopt(MPSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt_true, sizeof(int));
      if (res < 0)
      {
         closesocket(MPSocket);
         MPSocket = INVALID_SOCKET;
         return false;
      }

      sockaddr_t saddr;
      saddr.sa_family = AF_INET;
      *(u32*)&saddr.sa_data[2] = htonl(INADDR_ANY);
      *(u16*)&saddr.sa_data[0] = htons(7064);
      res = bind(MPSocket, &saddr, sizeof(sockaddr_t));
      if (res < 0)
      {
         closesocket(MPSocket);
         MPSocket = INVALID_SOCKET;
         return false;
      }

      res = setsockopt(MPSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt_true, sizeof(int));
      if (res < 0)
      {
         closesocket(MPSocket);
         MPSocket = INVALID_SOCKET;
         return false;
      }

      MPSendAddr.sa_family = AF_INET;
      *(u32*)&MPSendAddr.sa_data[2] = htonl(INADDR_BROADCAST);
      *(u16*)&MPSendAddr.sa_data[0] = htons(7064);

      return true;
   }

   void MP_DeInit()
   {
      if (MPSocket >= 0)
         closesocket(MPSocket);

#ifdef _WIN32
      WSACleanup();
#endif // __WXMSW__
   }

   int MP_SendPacket(u8* data, int len)
   {
      if (MPSocket < 0)
      {
         printf("MP_SendPacket: early return (%d)\n", len);
         return 0;
      }

      if (len > 2048-8)
      {
         printf("MP_SendPacket: error: packet too long (%d)\n", len);
         return 0;
      }

      *(u32*)&PacketBuffer[0] = htonl(0x4946494E); // NIFI
      PacketBuffer[4] = NIFI_VER;
      PacketBuffer[5] = 0;
      *(u16*)&PacketBuffer[6] = htons(len);
      memcpy(&PacketBuffer[8], data, len);

      int slen = sendto(MPSocket, (const char*)PacketBuffer, len+8, 0, &MPSendAddr, sizeof(sockaddr_t));
      if (slen < 8) return 0;
      return slen - 8;

   }

   int MP_RecvPacket(u8* data, bool block)
   {
      if (MPSocket < 0)
      {
         printf("MP_RecvPacket: early return\n");
         return 0;
      }

      fd_set fd;
      struct timeval tv;

      FD_ZERO(&fd);
      FD_SET(MPSocket, &fd);
      tv.tv_sec = 0;
      tv.tv_usec = block ? 5000 : 0;

      if (!select(MPSocket+1, &fd, 0, 0, &tv))
      {
         return 0;
      }

      sockaddr_t fromAddr;
      socklen_t fromLen = sizeof(sockaddr_t);
      int rlen = recvfrom(MPSocket, (char*)PacketBuffer, 2048, 0, &fromAddr, &fromLen);
      if (rlen < 8+24)
      {
         return 0;
      }
      rlen -= 8;

      if (ntohl(*(u32*)&PacketBuffer[0]) != 0x4946494E)
      {
         return 0;
      }

      if (PacketBuffer[4] != NIFI_VER)
      {
         return 0;
      }

      if (ntohs(*(u16*)&PacketBuffer[6]) != rlen)
      {
        return 0;
      }

      memcpy(data, &PacketBuffer[8], rlen);
      return rlen;
   }

   bool LAN_Init()
   {
#ifdef HAVE_PCAP
    if (Config::DirectLAN)
    {
        if (!LAN_PCap::Init(true))
            return false;
    }
    else
    {
        if (!LAN_Socket::Init())
            return false;
    }

    return true;
#else
   return false;
#endif
   }

   void LAN_DeInit()
   {
      // checkme. blarg
      //if (Config::DirectLAN)
      //    LAN_PCap::DeInit();
      //else
      //    LAN_Socket::DeInit();
#ifdef HAVE_PCAP
      LAN_PCap::DeInit();
      LAN_Socket::DeInit();
#endif
   }

   int LAN_SendPacket(u8* data, int len)
   {
#ifdef HAVE_PCAP
      if (Config::DirectLAN)
         return LAN_PCap::SendPacket(data, len);
      else
         return LAN_Socket::SendPacket(data, len);
#else
      return 0;
#endif
   }

   int LAN_RecvPacket(u8* data)
   {
#ifdef HAVE_PCAP
      if (Config::DirectLAN)
         return LAN_PCap::RecvPacket(data);
      else
         return LAN_Socket::RecvPacket(data);
#else
      return 0;
#endif
   }

#ifdef HAVE_OPENGL
   void* GL_GetProcAddress(const char* proc)
   {
      return (void*)hw_render.get_proc_address(proc);
   }
#endif
};

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing.fps            = 32.0f * 1024.0f * 1024.0f / 560190.0f;
   info->timing.sample_rate    = 32.0f * 1024.0f;
   info->geometry.base_width   = screen_layout_data.buffer_width;
   info->geometry.base_height  = screen_layout_data.buffer_height;
   info->geometry.max_width    = screen_layout_data.buffer_width;
   info->geometry.max_height   = screen_layout_data.buffer_height;
   info->geometry.aspect_ratio = (float)screen_layout_data.buffer_width / (float)screen_layout_data.buffer_height;
}

#ifdef HAVE_OPENGL
bool setup_opengl(void)
{
   if (!OpenGL_BuildShaderProgram(vertex_shader, fragment_shader, shader, "ScreenShader"))
      return false;

   glBindAttribLocation(shader[2], 0, "vPosition");
   glBindAttribLocation(shader[2], 1, "vTexcoord");
   glBindFragDataLocation(shader[2], 0, "oColor");

   if (!OpenGL_LinkShaderProgram(shader))
      return false;

   GLuint uni_id;

   uni_id = glGetUniformBlockIndex(shader[2], "uConfig");
   glUniformBlockBinding(shader[2], uni_id, 16);

   glUseProgram(shader[2]);
   uni_id = glGetUniformLocation(shader[2], "ScreenTex");
   glUniform1i(uni_id, 0);
   uni_id = glGetUniformLocation(shader[2], "_3DTex");
   glUniform1i(uni_id, 1);

   memset(&GL_ShaderConfig, 0, sizeof(GL_ShaderConfig));

   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(GL_ShaderConfig), &GL_ShaderConfig, GL_STATIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER, 16, ubo);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(screen_vertices), NULL, GL_STATIC_DRAW);

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glEnableVertexAttribArray(0); // position
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)(0));
   glEnableVertexAttribArray(1); // texcoord
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)(2*4));

   glGenTextures(1, &screen_framebuffer_texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, screen_framebuffer_texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 256*3 + 1, 192*2, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);

   update_opengl = true;

   return true;
}

static void context_reset(void)
{
   if(using_opengl)
      GPU3D::DeInitRenderer();

   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

   if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
      return;

   setup_opengl();

   if(using_opengl)
      GPU3D::InitRenderer(true);

   initialized_opengl = true;
   using_opengl = true;
}

static void context_destroy(void)
{
   initialized_opengl = false;
}

static bool context_framebuffer_lock(void *data)
{
    return false;
}

bool initialize_opengl(void)
{
   glsm_ctx_params_t params = {0};

   params.context_type     = RETRO_HW_CONTEXT_OPENGL;
   params.major            = 3;
   params.minor            = 3;
   params.context_reset    = context_reset;
   params.context_destroy  = context_destroy;
   params.environ_cb       = environ_cb;
   params.stencil          = false;
   params.framebuffer_lock = context_framebuffer_lock;

   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
   {
      log_cb(RETRO_LOG_ERROR, "Could not setup glsm, falling back to software rasterization.\n");
      return false;
   }

   return true;
}

void destroy_opengl(void)
{
   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_DESTROY, NULL))
   {
      log_cb(RETRO_LOG_ERROR, "Could not destroy glsm context.\n");
   }
}
#endif

static struct retro_rumble_interface rumble;

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   environ_cb = cb;

#ifdef HAVE_OPENGL
   std::string opengl_resolution = "OpenGL Internal Resolution; ";

   static const int MAX_SCALE = 8;

   char temp[100];
   for(int i = 1; i <= MAX_SCALE; i++)
   {
      temp[0] = 0;
      snprintf(temp, sizeof(temp), "%ix native (%ix%i)", i, VIDEO_WIDTH * i, VIDEO_HEIGHT * i);
      std::string param = temp;

      opengl_resolution.append(param);

      if(i != MAX_SCALE)
         opengl_resolution.append("|");
   }
#endif

  static const retro_variable values[] =
   {
      { "melonds_boot_directly", "Boot game directly; enabled|disabled" },
      { "melonds_screen_layout", "Screen Layout; Top/Bottom|Bottom/Top|Left/Right|Right/Left|Top Only|Bottom Only" },
      { "melonds_threaded_renderer", "Threaded software renderer; disabled|enabled" },
      { "melonds_touch_mode", "Touch mode; disabled|Mouse|Touch" },
#ifdef HAVE_OPENGL
      { "melonds_opengl_renderer", "OpenGL Renderer (Restart); disabled|enabled" },
      { "melonds_opengl_resolution", opengl_resolution.c_str() },
#endif
      { 0, 0 }
   };

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)values);

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;

   static const struct retro_controller_description controllers[] = {
      { "Nintendo DS", RETRO_DEVICE_JOYPAD },
      { NULL, 0 },
   };

   static const struct retro_controller_info ports[] = {
      { controllers, 1 },
      { NULL, 0 },
   };

   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   vfs_iface_info.required_interface_version = FILESTREAM_REQUIRED_VFS_VERSION;
   vfs_iface_info.iface = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_reset(void)
{
   NDS::Reset();
}

static void update_input(void)
{
   input_poll_cb();

   uint16_t keys = 0;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A)) << 0;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B)) << 1;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT)) << 2;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) << 3;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) << 4;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) << 5;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) << 6;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) << 7;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R)) << 8;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L)) << 9;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X)) << 10;
   keys |= (!!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y)) << 11;

   for (uint8_t i = 0; i < 12; i++) {
      bool key = !!((keys >> i) & 1);
      uint8_t nds_key = i > 9 ? i + 6 : i;


      if (key) {
         NDS::PressKey(nds_key);
      } else {
         NDS::ReleaseKey(nds_key);
      }
   }

   if(current_screen_layout != ScreenLayout::TopOnly)
   {
      switch(current_touch_mode)
      {
         case TouchMode::Disabled:
            touching = false;
            break;
         case TouchMode::Mouse:
            {
               int16_t mouse_x = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
               int16_t mouse_y = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

               touching = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);

               touch_x = Clamp(touch_x + mouse_x, 0, VIDEO_WIDTH);
               touch_y = Clamp(touch_y + mouse_y, 0, VIDEO_HEIGHT);
            }

            break;
         case TouchMode::Touch:
            if(input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED))
            {
               int16_t pointer_x = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
               int16_t pointer_y = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);

               int x = ((int)pointer_x + 0x8000) * screen_layout_data.buffer_width / 0x10000;
               int y = ((int)pointer_y + 0x8000) * screen_layout_data.buffer_height / 0x10000;

               if ((x >= screen_layout_data.touch_offset_x) && (x < screen_layout_data.touch_offset_x + screen_layout_data.screen_width) &&
                     (y >= screen_layout_data.touch_offset_y) && (y < screen_layout_data.touch_offset_y + screen_layout_data.screen_height))
               {
                  touching = true;

                  touch_x = (x - screen_layout_data.touch_offset_x) * VIDEO_WIDTH / screen_layout_data.screen_width;
                  touch_y = (y - screen_layout_data.touch_offset_y) * VIDEO_HEIGHT / screen_layout_data.screen_height;
               }
            }
            else if(touching)
            {
               touching = false;
            }

            break;
      }
   }
   else
   {
      touching = false;
   }

   if(touching)
   {
      NDS::TouchScreen(touch_x, touch_y);
      NDS::PressKey(16+6);
   }
   else
   {
      NDS::ReleaseScreen();
      NDS::ReleaseKey(16+6);
   }
}


static void check_variables(bool init)
{
   struct retro_variable var = {0};

   var.key = "melonds_boot_directly";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         Config::DirectBoot = false;
      else
         Config::DirectBoot = true;
   }

   ScreenLayout layout;
   var.key = "melonds_screen_layout";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Top/Bottom"))
         layout = ScreenLayout::TopBottom;
      else if (!strcmp(var.value, "Bottom/Top"))
         layout = ScreenLayout::BottomTop;
      else if (!strcmp(var.value, "Left/Right"))
         layout = ScreenLayout::LeftRight;
      else if (!strcmp(var.value, "Right/Left"))
         layout = ScreenLayout::RightLeft;
      else if (!strcmp(var.value, "Top Only"))
         layout = ScreenLayout::TopOnly;
      else if (!strcmp(var.value, "Bottom Only"))
         layout = ScreenLayout::BottomOnly;
   } else {
      layout = ScreenLayout::TopBottom;
   }

   var.key = "melonds_threaded_renderer";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         Config::Threaded3D = true;
      else
         Config::Threaded3D = false;
   }

   TouchMode new_touch_mode;

   var.key = "melonds_touch_mode";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Mouse"))
         new_touch_mode = TouchMode::Mouse;
      else if (!strcmp(var.value, "Touch"))
         new_touch_mode = TouchMode::Touch;
      else
         new_touch_mode = TouchMode::Disabled;
   }

#ifdef HAVE_OPENGL
   bool gl_update = false;

   if(current_touch_mode != new_touch_mode) // Hide the cursor
      gl_update = true;

   if(init) // Until crashes are fixed, this should only be set on init
   {
      var.key = "melonds_opengl_renderer";
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "enabled"))
            Config::_3DRenderer = true;
         else
            Config::_3DRenderer = false;
      }
   }

   var.key = "melonds_opengl_resolution";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int first_char_val = (int)var.value[0];
      int scaleing = Clamp(first_char_val - 48, 0, 8);

      if(Config::GL_ScaleFactor != scaleing)
         gl_update = true;

      Config::GL_ScaleFactor = scaleing;
   }
   else
   {
      Config::GL_ScaleFactor = 1;
   }

   if((using_opengl && gl_update) || layout != current_screen_layout)
      update_opengl = true;
#endif

   current_touch_mode = new_touch_mode;

   update_screenlayout(layout, &screen_layout_data, Config::_3DRenderer);
}

static void audio_callback(void)
{
   static int16_t buffer[0x1000];
   u32 avail = SPU::Available();
   if(avail > sizeof(buffer) / (2 * sizeof(int16_t)))
      avail = sizeof(buffer) / (2 * sizeof(int16_t));

   SPU::ReadOutput(buffer, avail);
   audio_cb(buffer, avail);
}

void copy_screen(ScreenLayoutData *data, uint32_t* src, unsigned offset)
{
   if (data->direct_copy)
   {
      memcpy((uint32_t *)data->buffer_ptr + offset, src, data->screen_width * data->screen_height * data->pixel_size);
   } else {
      unsigned y;
      for (y = 0; y < data->screen_height; y++)
      {
         memcpy((uint16_t *)data->buffer_ptr + offset + (y * data->screen_width * data->pixel_size),
            src + (y * data->screen_width), data->screen_width * data->pixel_size);
      }
   }
}

void draw_cursor(ScreenLayoutData *data, int32_t x, int32_t y)
{
   uint32_t* base_offset = (uint32_t*)data->buffer_ptr;

   uint32_t start_y = Clamp(y - CURSOR_SIZE, 0, data->screen_height);
   uint32_t end_y = Clamp(y + CURSOR_SIZE, 0, data->screen_height);

   for (uint32_t y = start_y; y < end_y; y++)
   {
      uint32_t start_x = Clamp(x - CURSOR_SIZE, 0, data->screen_width);
      uint32_t end_x = Clamp(x + CURSOR_SIZE, 0, data->screen_width);

      for (uint32_t x = start_x; x < end_x; x++)
      {
         uint32_t* offset = base_offset + ((y + data->touch_offset_y) * data->buffer_width) + ((x + data->touch_offset_x));
         uint32_t pixel = *offset;
         *(uint32_t*)offset = (0xFFFFFF - pixel) | 0xFF000000;
      }
   }
}

void retro_run(void)
{
   if (current_renderer == CurrentRenderer::None)
   {
 #ifdef HAVE_OPENGL
         if (Config::_3DRenderer && using_opengl)
         {
            glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
            GPU3D::InitRenderer(true);
            glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);

            current_renderer = CurrentRenderer::OpenGL;
         }
         else
         {
#endif
            GPU3D::InitRenderer(false);
            current_renderer = CurrentRenderer::Software;
#ifdef HAVE_OPENGL
         }
#endif
   }

   update_input();
#ifdef HAVE_OPENGL
   if(using_opengl)
      glsm_ctl(GLSM_CTL_STATE_BIND, NULL);

   if(using_opengl && update_opengl)
      GPU3D::UpdateRendererConfig();
#endif

   NDS::RunFrame();

   int frontbuf = GPU::FrontBuffer;

   if(using_opengl)
   {
#ifdef HAVE_OPENGL
      bool virtual_cursor = current_touch_mode == TouchMode::Mouse;
      glBindFramebuffer(GL_FRAMEBUFFER, glsm_get_current_framebuffer());

      if(update_opengl)
      {
         update_opengl = false;

         GL_ShaderConfig.uScreenSize[0] = screen_layout_data.buffer_width;
         GL_ShaderConfig.uScreenSize[1] = screen_layout_data.buffer_height;
         GL_ShaderConfig.u3DScale = Config::GL_ScaleFactor;
         GL_ShaderConfig.cursorPos[0] = -1;
         GL_ShaderConfig.cursorPos[1] = -1;
         GL_ShaderConfig.cursorPos[2] = -1;
         GL_ShaderConfig.cursorPos[3] = -1;

         glBindBuffer(GL_UNIFORM_BUFFER, ubo);
         void* unibuf = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
         if (unibuf) memcpy(unibuf, &GL_ShaderConfig, sizeof(GL_ShaderConfig));
         glUnmapBuffer(GL_UNIFORM_BUFFER);

         float screen_width = (float)screen_layout_data.screen_width;
         float screen_height = (float)screen_layout_data.screen_height;

         float top_screen_x = 0.0f;
         float top_screen_y = 0.0f;

         float bottom_screen_x = 0.0f;
         float bottom_screen_y = 0.0f;

         switch (current_screen_layout)
         {
            case ScreenLayout::TopBottom:
               bottom_screen_y = screen_height;
               break;
           case ScreenLayout::BottomTop:
               top_screen_y = screen_height;
               break;
           case ScreenLayout::LeftRight:
               bottom_screen_x = screen_width;
               break;
           case ScreenLayout::RightLeft:
               top_screen_x = screen_width;
               break;
           case ScreenLayout::TopOnly:
               bottom_screen_y = screen_height; // Meh, let's just hide it
               break;
           case ScreenLayout::BottomOnly:
               top_screen_y = screen_height; // ditto
               break;
         }

         #define SETVERTEX(i, x, y, t_x, t_y) \
            screen_vertices[(4 * i) + 0] = x; \
            screen_vertices[(4 * i) + 1] = y; \
            screen_vertices[(4 * i) + 2] = t_x; \
            screen_vertices[(4 * i) + 3] = t_y;

         // top screen
         SETVERTEX(0, top_screen_x, top_screen_y, 0.0f, 0.0f); // top left
         SETVERTEX(1, top_screen_x, top_screen_y + screen_height, 0.0f, VIDEO_HEIGHT); // bottom left
         SETVERTEX(2, top_screen_x + screen_width, top_screen_y + screen_height, VIDEO_WIDTH,  VIDEO_HEIGHT); // bottom right
         SETVERTEX(3, top_screen_x, top_screen_y, 0.0f, 0.0f); // top left
         SETVERTEX(4, top_screen_x + screen_width, top_screen_y, VIDEO_WIDTH, 0.0f); // top right
         SETVERTEX(5, top_screen_x + screen_width, top_screen_y + screen_height, VIDEO_WIDTH,  VIDEO_HEIGHT); // bottom right

         // bottom screen
         SETVERTEX(6, bottom_screen_x, bottom_screen_y, 0.0f, VIDEO_HEIGHT); // top left
         SETVERTEX(7, bottom_screen_x, bottom_screen_y + screen_height, 0.0f, VIDEO_HEIGHT * 2.0f); // bottom left
         SETVERTEX(8, bottom_screen_x + screen_width, bottom_screen_y + screen_height, VIDEO_WIDTH,  VIDEO_HEIGHT * 2.0f); // bottom right
         SETVERTEX(9, bottom_screen_x, bottom_screen_y, 0.0f, VIDEO_HEIGHT); // top left
         SETVERTEX(10, bottom_screen_x + screen_width, bottom_screen_y, VIDEO_WIDTH, VIDEO_HEIGHT); // top right
         SETVERTEX(11, bottom_screen_x + screen_width, bottom_screen_y + screen_height, VIDEO_WIDTH, VIDEO_HEIGHT * 2.0f); // bottom right

         glBindBuffer(GL_ARRAY_BUFFER, vbo);
         glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(screen_vertices), screen_vertices);
      }

      if(virtual_cursor)
      {
         GL_ShaderConfig.cursorPos[0] = touch_x - CURSOR_SIZE;
         GL_ShaderConfig.cursorPos[1] = touch_y - CURSOR_SIZE;
         GL_ShaderConfig.cursorPos[2] = touch_x + CURSOR_SIZE;
         GL_ShaderConfig.cursorPos[3] = touch_y + CURSOR_SIZE;

         glBindBuffer(GL_UNIFORM_BUFFER, ubo);
         void* unibuf = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
         if (unibuf) memcpy(unibuf, &GL_ShaderConfig, sizeof(GL_ShaderConfig));
         glUnmapBuffer(GL_UNIFORM_BUFFER);
      }

      glDisable(GL_DEPTH_TEST);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_BLEND);
      glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

      glViewport(0, 0, screen_layout_data.buffer_width, screen_layout_data.buffer_height);

      OpenGL_UseShaderProgram(shader);

      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, screen_framebuffer_texture);

      if (GPU::Framebuffer[frontbuf][0] && GPU::Framebuffer[frontbuf][1])
      {
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256*3 + 1, 192, GL_RGBA_INTEGER,
                        GL_UNSIGNED_BYTE, GPU::Framebuffer[frontbuf][0]);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192, 256*3 + 1, 192, GL_RGBA_INTEGER,
                        GL_UNSIGNED_BYTE, GPU::Framebuffer[frontbuf][1]);
      }

      glActiveTexture(GL_TEXTURE1);
      GPU3D::GLRenderer::SetupAccelFrame();

      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLES, 0, 4*3);

      glFlush();

      glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);

      video_cb(RETRO_HW_FRAME_BUFFER_VALID, screen_layout_data.buffer_width, screen_layout_data.buffer_height, 0);
#endif
   }
   else
   {
      if(screen_layout_data.enable_top_screen)
         copy_screen(&screen_layout_data, GPU::Framebuffer[frontbuf][0], screen_layout_data.top_screen_offset);
      if(screen_layout_data.enable_bottom_screen)
         copy_screen(&screen_layout_data, GPU::Framebuffer[frontbuf][1], screen_layout_data.bottom_screen_offset);

      if(current_touch_mode == TouchMode::Mouse && current_screen_layout != ScreenLayout::TopOnly)
         draw_cursor(&screen_layout_data, touch_x, touch_y);

      video_cb((uint8_t*)screen_layout_data.buffer_ptr, screen_layout_data.buffer_width, screen_layout_data.buffer_height, screen_layout_data.buffer_width * sizeof(uint32_t));
   }

   audio_callback();

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      check_variables(false);

      struct retro_system_av_info updated_av_info;
      retro_get_system_av_info(&updated_av_info);
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &updated_av_info);
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y" },
      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
      return false;
   }

   check_variables(true);

#ifdef HAVE_OPENGL
   if (Config::_3DRenderer)
      initialize_opengl();
#endif

   if(!NDS::Init())
      return false;

   char game_name[256];
   fill_pathname_base_noext(game_name, info->path, sizeof(game_name));

   std::string save_path = std::string(retro_saves_directory) + std::string(1, platformDirSeparator) + std::string(game_name) + ".sav";

   NDS::LoadROM(info->path, save_path.c_str(), Config::DirectBoot);

   (void)info;
   if (!retro_firmware_status)
      return false;

   return true;
}

void retro_unload_game(void)
{
   NDS::DeInit();
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   return false;
}

size_t retro_serialize_size(void)
{
   return 7041996;
}

bool retro_serialize(void *data, size_t size)
{
   Savestate* savestate = new Savestate(data, size, true);
   NDS::DoSavestate(savestate);
   delete savestate;

   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   Savestate* savestate = new Savestate((void*)data, size, false);
   NDS::DoSavestate(savestate);
   delete savestate;

   return true;
}

void *retro_get_memory_data(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return NDS::MainRAM;
   else
      return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return 0x400000;
   else
      return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

namespace Config
{
   int _3DRenderer;
   int Threaded3D;

   int GL_ScaleFactor;
   int GL_Antialias;

   bool DirectBoot;
   bool DirectLAN;

   ConfigEntry ConfigFile[] =
   {
      {"3DRenderer", 0, &_3DRenderer, 1, NULL, 0},
      {"Threaded3D", 0, &Threaded3D, 1, NULL, 0},

      {"GL_ScaleFactor", 0, &GL_ScaleFactor, 1, NULL, 0},
      {"GL_Antialias", 0, &GL_Antialias, 0, NULL, 0},

      {"", -1, NULL, 0, NULL, 0}
   };

   void Load()
   {

   }

   void Save()
   {

   }
}
