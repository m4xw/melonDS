#include <cstdio>
#include "screenlayout.h"

ScreenLayout current_screen_layout;
ScreenLayoutData screen_layout_data;

void update_screenlayout(ScreenLayout layout, ScreenLayoutData *data, bool opengl)
{
    unsigned pixel_size = 4; // XRGB8888 is hardcoded for now, so it's fine
    data->pixel_size = pixel_size;

    unsigned scale = 1; // ONLY SUPPORTED BY OPENGL RENDERER

    unsigned old_size = data->buffer_stride * data->buffer_height;

    data->direct_copy = false;

    data->screen_width = VIDEO_WIDTH * scale;
    data->screen_height = VIDEO_HEIGHT * scale;

    switch (layout)
    {
        case ScreenLayout::TopBottom:
            data->enable_top_screen = true;
            data->enable_bottom_screen = true;
            data->direct_copy = true;

            data->buffer_width = data->screen_width;
            data->buffer_height = data->screen_height * 2;
            data->buffer_stride = data->screen_width * pixel_size;

            data->touch_offset_x = 0;
            data->touch_offset_y = data->screen_height;

            data->top_screen_offset = 0;
            data->bottom_screen_offset = data->buffer_width * data->screen_height;

            break;
        case ScreenLayout::BottomTop:
            data->enable_top_screen = true;
            data->enable_bottom_screen = true;
            data->direct_copy = true;

            data->buffer_width = data->screen_width;
            data->buffer_height = data->screen_height * 2;
            data->buffer_stride = data->screen_width * pixel_size;

            data->touch_offset_x = 0;
            data->touch_offset_y = 0;

            data->top_screen_offset = data->buffer_width * data->screen_height;
            data->bottom_screen_offset = 0;

            break;
        case ScreenLayout::LeftRight:
            data->enable_top_screen = true;
            data->enable_bottom_screen = true;

            data->buffer_width = data->screen_width * 2;
            data->buffer_height = data->screen_height;
            data->buffer_stride = data->screen_width * 2 * pixel_size;

            data->touch_offset_x = data->screen_width;
            data->touch_offset_y = 0;

            data->top_screen_offset = 0;
            data->bottom_screen_offset = (VIDEO_WIDTH * 2);

            break;
        case ScreenLayout::RightLeft:
            data->enable_top_screen = true;
            data->enable_bottom_screen = true;

            data->buffer_width = data->screen_width * 2;
            data->buffer_height = data->screen_height;
            data->buffer_stride = data->screen_width * 2 * pixel_size;

            data->touch_offset_x = 0;
            data->touch_offset_y = 0;

            data->top_screen_offset = (VIDEO_WIDTH * 2);
            data->bottom_screen_offset = 0;

            break;
        case ScreenLayout::TopOnly:
            data->enable_top_screen = true;
            data->enable_bottom_screen = false;
            data->direct_copy = true;

            data->buffer_width = data->screen_width;
            data->buffer_height = data->screen_height;
            data->buffer_stride = data->screen_width * pixel_size;

            // should be disabled in top only
            data->touch_offset_x = 0;
            data->touch_offset_y = 0;

            data->top_screen_offset = 0;

            break;
        case ScreenLayout::BottomOnly:
            data->enable_top_screen = false;
            data->enable_bottom_screen = true;
            data->direct_copy = true;

            data->buffer_width = data->screen_width;
            data->buffer_height = data->screen_height;
            data->buffer_stride = data->screen_width * pixel_size;

            data->touch_offset_x = 0;
            data->touch_offset_y = 0;

            data->bottom_screen_offset = 0;

            break;
    }

    if (opengl)
    {
        if(data->buffer_ptr != nullptr) free(data->buffer_ptr); // not needed anymore :)
    }
    else
    {
        unsigned new_size = data->buffer_stride * data->buffer_height;

        if (old_size != new_size || data->buffer_ptr == nullptr)
        {
            if(data->buffer_ptr != nullptr) free(data->buffer_ptr);
            data->buffer_ptr = (uint16_t*)malloc(new_size);

            memset(data->buffer_ptr, 0, new_size);
        }
    }

    current_screen_layout = layout;
}