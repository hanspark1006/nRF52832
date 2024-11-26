#include "m_event_def.h"
#include "macros_common.h"

const char* event_type_2_str(event_type_t evt)
{
    switch (evt)
    {
        case_str(EVT_connect);
        case_str(EVT_disconnected);
        case_str(EVT_received);
        default : 
            break;
    }
    return "Unknown event";
}