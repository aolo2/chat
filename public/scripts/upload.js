const ATTACHMENT_TYPE = {
    EXT_OTHER: 0,
    EXT_JPEG: 1,
    EXT_PNG: 2,
    EXT_BMP: 3,
    EXT_GIF: 4,
    
    EXT_AUDIO: 5,
    EXT_VIDEO: 6,
    EXT_IMAGE: 7,
    EXT_TEXT: 9,
    EXT_ARCHIVE: 10,
};

const PREVIEW_LVL0 = 768;
const PREVIEW_LVL1 = 256;

function uploader_create(input_item, options) {

    const uploader = {
        'file_input': input_item,
    };

    uploader.url_create = options.url_create || null;
    uploader.url_status = options.url_status || null;
    uploader.url_upload = options.url_upload || null;
    uploader.url_cancel = options.url_cancel || null;

    uploader.on_start    = options.on_start    || null;
    uploader.on_complete = options.on_complete || null;
    uploader.on_progress = options.on_progress || null;
    uploader.on_error    = options.on_error || ((msg) => console.error('uploader:', msg)); 

    uploader.data_func   = options.data_func || null;

    if (!uploader.url_create) {
        console.error(`uploader: 'url_create' option is required`);
        return null;
    }

    if (!uploader.url_status) {
        console.error(`uploader: 'url_status' option is required`);
        return null;
    }

    if (!uploader.url_upload) {
        console.error(`uploader: 'url_upload' option is required`);
        return null;
    }

    uploader.file_input.addEventListener('change', (e) => uploader_submit(e, uploader));

    return uploader;
}

function uploader_server_ext(mime_type) {
    // NOTE(aolo2): we are in heuristic town
    switch (mime_type) {
        case 'image/jpeg': return ATTACHMENT_TYPE.EXT_JPEG;
        case 'image/png':  return ATTACHMENT_TYPE.EXT_PNG;
        case 'image/gif':  return ATTACHMENT_TYPE.EXT_GIF;
        case 'image/bmp':  return ATTACHMENT_TYPE.EXT_BMP;

        default: {
            if (mime_type.startsWith('audio/')) {
                return ATTACHMENT_TYPE.EXT_AUDIO;
            } else if (mime_type.startsWith('video/')) {
                return ATTACHMENT_TYPE.EXT_VIDEO;
            } else if (mime_type.startsWith('image/')) {
                return ATTACHMENT_TYPE.EXT_IMAGE;
            } else if (['application/pdf', 'application/msword'].includes(mime_type) 
                || mime_type.startsWith('text/') || mime_type.startsWith('multipart/') 
                || mime_type.startsWith('message/')) {
                return ATTACHMENT_TYPE.EXT_TEXT;
            } else if (['application/zip', 'application/gzip', 'application/x-tar', 'application/x-rar-compressed'].includes(mime_type)) {
                return ATTACHMENT_TYPE.EXT_ARCHIVE;
            } else {
                return ATTACHMENT_TYPE.EXT_OTHER;
            }   
        }
    }
}

function uploader_image_dimensions(url){
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => {
            // This mirrors the resizing code on the media server
            let preview_width = PREVIEW_LVL0;
            let preview_height = PREVIEW_LVL0;
            
            if (img.height <= preview_height && img.width <= preview_width) {
                preview_height = img.height;
                preview_width = img.width;
            } else if (img.width > img.height) {
                preview_height *= img.height / img.width;
            } else {
                preview_width *= img.width / img.height;
            }

            // Trunc because these are integers on the server
            preview_width = Math.floor(preview_width);
            preview_height = Math.floor(preview_height);
            
            resolve({'width': preview_width, 'height': preview_height});
        }
        img.onerror = reject;
        img.src = url;
    });
}

async function uploader_manual_submit(uploader, file) {
    const ext = uploader_server_ext(file.type);
    const start_result = await uploader_start(uploader, file);

    if (start_result.error !== null) {
        uploader.on_error(start_result.error, uploader.data);
        return false;   
    }

    file.id = start_result.file_id;
    file.extension = ext;
    file.original = file;

    if (is_supported_image(ext)) {
        const url = URL.createObjectURL(file);
        const dimensions = await uploader_image_dimensions(url);

        if (dimensions) {
            file.width = dimensions.width;
            file.height = dimensions.height;
        }
    }

    uploader.total = file.size;

    localStorage.setItem(`uploader-${uploader.id}-file`, file.id);
    localStorage.setItem(`uploader-${uploader.id}-size`, file.size);

    await uploader_upload(uploader, file);
}

async function uploader_status(uploader, file_id) {
    const resp = await fetch(uploader.url_status, { 
        'method': 'POST', 
        'headers': { 'X-File-Id': file_id }
    });

    if (resp.status === 200) {
        return +(await resp.text());
    }

    return null;
}

async function uploader_resume(uploader) {
    const file_id = localStorage.getItem(`uploader-${uploader.id}-file`);
    const file_size = +localStorage.getItem(`uploader-${uploader.id}-size`);

    if (file_id !== null && uploader.file_input.files.length === 1) {
        const original = uploader.file_input.files[0];
        const file = {
            'name': original.name,
            'size': original.size,
            'original': original
        };
        const already_uploaded = await uploader_status(uploader, file_id);
        if (already_uploaded !== null) {
            file.id = file_id;
            file.original = uploader.file_input.files[0];
            uploader.total = file_size;
            await uploader_upload(uploader, file, already_uploaded);
        }
    }
}

async function uploader_submit(e, uploader) {
    e.preventDefault();

    for (const file of uploader.file_input.files) {
        uploader_manual_submit(uploader, file);
    }

    return false;
}

async function uploader_start(uploader, file)  {
    uploader.data = uploader.data_func ? uploader.data_func() : null;

    const body = file.name;
    const resp = await fetch(uploader.url_create, { 'method': 'POST', 'body': body });

    if (resp.status !== 200 && resp.status !== 201) {
        return { 'error': `url '${uploader.url_create}' returned status ${resp.status}` };
    }

    return {
        'error': null,
        'file_id': resp.headers.get('X-File-Id')
    };
}

async function uploader_upload(uploader, file, already_uploaded = 0) {
    const form_data = new FormData();
    const xhr = new XMLHttpRequest(); // XHR allows getting progress updates (unlike fetch) 
    const chunk = already_uploaded > 0 ? file.original.slice(already_uploaded) : file.original;

    if (uploader.on_start) {
        uploader.on_start(file, uploader.data);
    }

    form_data.append('file', chunk);

    xhr.open('POST', uploader.url_upload);
    xhr.setRequestHeader('X-File-Id', file.id);

    xhr.addEventListener('load', (e) => {
        localStorage.removeItem(`uploader-${uploader.id}-file`);
        localStorage.removeItem(`uploader-${uploader.id}-size`);
        uploader.on_complete(file, uploader.data);
    });

    xhr.upload.addEventListener('progress', (e) => {
        const left = e.total - e.loaded;
        const full_loaded = uploader.total - left;
        if (uploader.on_progress) {
            uploader.on_progress(file.id, full_loaded, uploader.total, uploader.data);
        }
    });

    xhr.addEventListener('error', () => {
        localStorage.removeItem(`uploader-${uploader.id}-file`);
        localStorage.removeItem(`uploader-${uploader.id}-size`);
        uploader.on_error('XHR error', uploader.data);
    });

    // TODO: xhr.abort();

    xhr.send(form_data);
}
