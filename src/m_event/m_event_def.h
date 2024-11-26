#pragma once

typedef enum
{
    EVT_none,
    EVT_connect,
    EVT_disconnected,
    EVT_received
} event_type_t;

const char* event_type_2_str(event_type_t evt);