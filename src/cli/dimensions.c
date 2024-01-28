#include "../shared/shared.h"
#include "../websocket/websocket.h"

#include "../shared/aux.c"
#include "../shared/log.c"

#define STB_IMAGE_IMPLEMENTATION
#include "../media/external/stb_image.h"

static bool
write_dimensions(char *image_fullpath, char *data_fullpath)
{
    /* Go through all records in all files and try to set width/height for ATTACH records */
    
    if (chdir(data_fullpath) == -1) {
        log_fperror(__func__, "chdir data");
        return(false);
    }
    
    if (chdir("messages") == -1) {
        log_fperror(__func__, "chdir data/messages");
        return(false);
    }
    
    DIR *dir = opendir(".");
    struct dirent *ent;
    
    int total_written = 0;
    
    char attach_fullpath[PATH_MAX];
    
    if (dir) {
        while ((ent = readdir(dir))) {
            if (ent->d_type != DT_REG) {
                continue;
            }
            
            int fd = open(ent->d_name, O_RDWR);
            
            if (fd == -1) {
                log_warning("Could not open file %s\n", ent->d_name);
                log_perror("open");
                continue;
            }
            
            log_info("Opened file %s\n", ent->d_name);
            u64 file_size = get_file_size(fd);
            
            if (file_size > 0) {
                char *data = mmap(0, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                
                if (data == MAP_FAILED) {
                    log_perror("mmap");
                    goto nextloop;
                }
                
                u64 offset = BDISK_HEADER_SIZE;
                
                while (offset < file_size) {
                    struct bc_persist_record *record = (struct bc_persist_record *) (data + offset);
                    
                    if (record->type == WS_ATTACH) {
                        u64 file_id = record->attach.file_id;
                        snprintf(attach_fullpath, PATH_MAX, "%s/%lx-0", image_fullpath, file_id);
                        
                        int width, height;
                        int comp;
                        
                        if (stbi_info(attach_fullpath, &width, &height, &comp)) {
                            record->attach.width = width;
                            record->attach.height = height;
                            ++total_written;
                        }
                    }
                    
                    offset += record->size;
                }
                
                munmap(data, file_size);
            }
            
            nextloop:
            close(fd);
        }
    }
    
    log_info("Wrote dimensions of %d images\n", total_written);
    
    return(true);
}

int
main(int argc, char **argv)
{
    if (argc != 3) {
        log_error("Usage: %s image_folder data_folder\n", argv[0]);
        return(1);
    }
    
    char image_fullpath[PATH_MAX] = { 0 };
    char data_fullpath[PATH_MAX] = { 0 };
    
    if (!realpath(argv[1], image_fullpath)) {
        log_perror("realpath");
        return(1);
    }
    
    if (!realpath(argv[2], data_fullpath)) {
        log_perror("realpath");
        return(1);
    }
    
    log_info("Image folder: %s\n", image_fullpath);
    log_info("Data folder:  %s\n", data_fullpath);
    
    char y;
    
    printf("Looks good (y/N) ?\n");
    scanf("%c", &y);
    
    if (y != 'y' && y != 'Y') {
        return(1);
    }
    
    if (!write_dimensions(image_fullpath, data_fullpath)) {
        log_error("write_dimensions returned false\n");
        return(1);
    }
    
    log_info("Success!\n");
    
    return(0);
}