let thread_opened = false;
let opened_thread_id = null;
let thread_root = null;
let thread_content_block = null;
function thread_open(thread_id) {
    thread_opened = true;
    opened_thread_id = thread_id;
    const channel_id = ls_get_current_channel_id();
    thread_content_block.set_channel(channel_id);
    thread_content_block.set_thread(opened_thread_id);
    thread_show();
}

function thread_close() {
    thread_opened = false;
    opened_thread_id = null;
    thread_hide();
}

function thread_init(root) {
    thread_root = root;
    let uploader_hidden_btn = root.querySelector('.upload-hidden-button');
    thread_content_block = new ContentBlock({
                                                scroller_root: root.querySelector('.scroller-container'),
                                                message_input_root: root.querySelector('.message-input'),
                                                block_root: root,
                                                uploader_btn: uploader_hidden_btn,
                                                drag_indicator: root.querySelector('.message-input-drag-indicator')
                                            })
    thread_content_block.init();
    let back_button = root.querySelector('.header-back-btn');
    back_button.addEventListener('click', thread_close);
}

function thread_show() {
    divs['main-page'].classList.add('thread-opened');
    thread_root.classList.remove('dhide');
}

function thread_hide() {
    divs['main-page'].classList.remove('thread-opened');
    thread_root.classList.add('dhide');
}