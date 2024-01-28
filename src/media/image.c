static struct bc_image
image_load(u64 file_id) {
    int w, h, comps;
    
    struct bc_image img = { 0 };
    
    snprintf(img.filename, 16 + 1, "%lx", file_id);
    
    stbi_uc *data = stbi_load(img.filename, &w, &h, &comps, 0);
    
    if (data == NULL) {
        //fprintf(stderr, "[ERROR] Failed to load image = %lx\n", file_id);
        return(img);
    }
    
    img.data = data;
    img.width = w;
    img.height = h;
    img.comps = comps;
    
    return(img);
}

static void
image_convert_to_level(struct bc_image img, int level)
{
    char preview_filename[sizeof(img.filename) + 2] = { 0 };
    
    memcpy(preview_filename, img.filename, sizeof(img.filename) - 1);
    
    preview_filename[strlen(img.filename)] = '-';
    preview_filename[strlen(img.filename) + 1] = level + '0';
    
    int rt = stbi_write_png(preview_filename, img.width, img.height, img.comps, img.data, 0);
    
    if (rt == 0) {
        fprintf(stderr, "[ERROR] Failed to convert image to png = %s\n", img.filename);
        return;
    }
    
    log_info("Saved file %s to disk\n", preview_filename);
}

static void
img_resize_to_level(struct bc_image img, int level)
{
    int preview_width, preview_height;
    
    preview_width = PREVIEW_LEVELS[level];
    preview_height = PREVIEW_LEVELS[level];
    
    if (img.height <= preview_height && img.width <= preview_width) {
        // Image is small enough already
        preview_height = img.height;
        preview_width = img.width;
    } else if (img.width > img.height) {
        // We need to shrink the width to the desired level and then adjust the height accordingly
        preview_height *= (float) img.height / img.width;
    } else {
        // We need to shrink the height to the desired level and then adjust the width accordingly
        preview_width *= (float) img.width / img.height;
    }
    
    //printf("%d %d\n", preview_width, preview_height);
    
    stbi_uc *output = malloc(preview_width * preview_height * img.comps);
    
    int rt = 1;
    
    if (preview_width == img.width && preview_height == img.height) {
        memcpy(output, img.data, img.width * img.height * img.comps);
    } else {
        rt = stbir_resize_uint8(img.data, img.width, img.height, 0, output, preview_width, preview_height, 0, img.comps);
    }
    
    if (rt == 0) {
        fprintf(stderr, "[ERROR] Failed to resize image = %s\n", img.filename);
        return;
    }
    
    img.data = output;
    img.width = preview_width;
    img.height = preview_height;
    
    image_convert_to_level(img, level);
}

static int
image_resize(struct bc_image img) {
    img_resize_to_level(img, 0);
    img_resize_to_level(img, 1);
    img_resize_to_level(img, 2);
    img_resize_to_level(img, 3);
    
    free(img.data);
    
    return(0);
}

static void
generate_all_missing_previews(char *dir_path)
{
    DIR *dir = opendir(dir_path);
    struct dirent *ent;
    
    if (dir) {
        while (on && (ent = readdir(dir))) {
            bool any_missing = false; /* so that we don't load the full image multiple times */
            
            if (ent->d_type != DT_REG) {
                continue;
            }
            
            int filename_length = strlen(ent->d_name);
            
            if (filename_length >= 2 && ent->d_name[filename_length - 2] != '-') {
                for (int level = 0; level <= 3; ++level) {
                    char filename[255 + 32] = { 0 };
                    snprintf(filename, 255 + 32, "%s-%d", ent->d_name, level);
                    
                    // man access:
                    // "F_OK tests for the existence of the file"
                    if (access(filename, F_OK) != 0) {
                        any_missing = true;
                        log_warning("Level %d preview doesn't exist for file %s\n", level, ent->d_name);
                    } else {
                        //log_info("File %s exists\n", filename);
                    }
                }
                
                if (any_missing) {
                    u64 file_id = strtoull(ent->d_name, NULL, 16);
                    struct bc_image img = image_load(file_id);
                    if (img.data) {
                        for (int level = 0; level <= 3; ++level) {
                            char filename[255 + 32] = { 0 };
                            snprintf(filename, 255 + 32, "%s-%d", ent->d_name, level);
                            if (access(filename, F_OK) != 0) {
                                img_resize_to_level(img, level);
                            }
                        }
                        free(img.data);
                    }
                }
            }
        }
        
        closedir(dir);
    }
}