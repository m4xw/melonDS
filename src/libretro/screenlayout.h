#include <cstring>
#include <stdint.h>
#include <stdlib.h>

#define VIDEO_WIDTH 256
#define VIDEO_HEIGHT 192

enum ScreenLayout
{
   TopBottom = 0,
   BottomTop = 1,
   LeftRight = 2,
   RightLeft = 3,
   TopOnly = 4,
   BottomOnly = 5,
};

struct ScreenLayoutData
{
    bool enable_top_screen;
    bool enable_bottom_screen;
    bool direct_copy;
    unsigned pixel_size;
    unsigned scale;

    unsigned screen_width;
    unsigned screen_height;
    unsigned top_screen_offset;
    unsigned bottom_screen_offset;

    unsigned touch_offset_x;
    unsigned touch_offset_y;

    unsigned buffer_width;
    unsigned buffer_height;
    unsigned buffer_stride;
    size_t buffer_len;
    uint16_t* buffer_ptr;
};

extern ScreenLayout current_screen_layout;
extern ScreenLayoutData screen_layout_data;

void update_screenlayout(ScreenLayout layout, ScreenLayoutData *data, bool opengl);
