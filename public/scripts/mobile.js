const LONG_TOUCH = 500;

let long_touch_timers = {};

function HTML(html) {
    const template = document.createElement('template');
    template.innerHTML = html.trim();
    return template.content.firstChild;
}

function start_touch(e, on_tap, on_hold, button_id) {
    const item = e.target;

    item.classList.add('active');

    const clear_touch = (e) => {
        clearTimeout(long_touch_timers[button_id]);
        
        if (on_tap) {
            item.removeEventListener('touchend', on_tap);
        }

        item.classList.remove('active');
        item.removeEventListener('touchend', clear_touch);
        item.removeEventListener('touchmove', clear_touch);
        item.removeEventListener('touchcancel', clear_touch);
    }

    long_touch_timers[button_id] = setTimeout(() => { 
        if (on_hold) on_hold(e);
        clear_touch(e); // e?
    }, LONG_TOUCH);

    if (on_tap) item.addEventListener('touchend', on_tap);

    item.addEventListener('touchend', clear_touch);
    item.addEventListener('touchmove', clear_touch);
    item.addEventListener('touchcancel', clear_touch);
}

function make_button(item, on_tap = null, on_hold = null) {
    const button_id = random_string();
    item.addEventListener('touchstart', (e) => start_touch(e, on_tap, on_hold, button_id));
}