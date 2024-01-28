static char *
record_data(struct bc_persist_record *record)
{
    if (!record) return(0);
    
    // Waiting for fixed-width enums got me like
    switch ((enum bc_record_type) record->type) { // cast type to enum for -Wswitch
        case WS_TITLE_CHANGED:
        case WS_MESSAGE:
        case WS_EDIT:
        case WS_ATTACH: {
            return(POINTER_INC(record, sizeof(*record)));
        }

        case WS_REPLY:
        case WS_PIN:
        case WS_DELETE:
        case WS_REACTION_REMOVE:
        case WS_REACTION_ADD:
        case WS_USER_LEFT:
        case WS_USER_JOINED: {
            break;
        }
        
        case WS_RECORD_TYPE_COUNT: {
            break;
        }
        
        // NOTE(aolo2): do NOT add a default case, so that -Wswitch catches new message types
    }
    
    return(0);
}

static bool
record_attach_ext_is_supported_image(u32 ext)
{
    bool result = (1 <= ext && ext <= 4) || (ext == 7);
    return(result);
}

static int
record_size(struct bc_persist_record *record)
{
    if (!record) return(0);
    
    int result = sizeof(*record);
    
    switch ((enum bc_record_type) record->type) { // cast type to enum for -Wswitch
        case WS_MESSAGE: {
            result += record->message.length;
            break;
        }
        
        case WS_EDIT: {
            result += record->edit.length;
            break;
        }

        case WS_TITLE_CHANGED: {
            result += record->title_changed.length;
            break;
        }
        
        case WS_ATTACH: {
            if (!record_attach_ext_is_supported_image(record->attach.file_ext)) {
                /* exts 1-4, 7 mean its a supported image with width and height and no filename */
                result += record->attach.filename_length;
            }
            
            break;
        }
        
        case WS_REPLY:
        case WS_PIN:
        case WS_DELETE:
        case WS_REACTION_REMOVE:
        case WS_REACTION_ADD:
        case WS_USER_LEFT: 
        case WS_USER_JOINED: {
            break;
        }
        
        case WS_RECORD_TYPE_COUNT: {
            break;
        }
        
        // NOTE(aolo2): do NOT add a default case, so that -Wswitch catches new message types
    }
    
    return(result);
}