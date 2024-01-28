let popup_title = null;
let popup_body = null;
let popup_buttons = null;
let popup_container = null;

let context_menu_touch_cancel = false;
let context_menu_touch_cancel_timer = null;

const popup_stack = [];

const CONTEXT_MENU_HIDE_TIMEOUT = 100;

function popup_init() {
    popup_title     = divs['popup'].querySelector('.title');
    popup_body      = divs['popup'].querySelector('.body');
    popup_buttons   = divs['popup'].querySelector('.buttons');
    popup_dimmer    = divs['popup-dimmer'];
    popup_container = divs['popup-container'];

    make_clickable(popup_dimmer, popup_hide);
}

function popup_show(title, text, buttons, bare=false) {
    if (bare) {
        popup_title.innerHTML = '';
        popup_body.innerHTML = text;
        popup_buttons.innerHTML = '';        
        popup.classList.add('notitle');
        popup_container.classList.remove('noevents');
        popup_dimmer.classList.remove('hidden');
        divs['popup'].classList.remove('hidden');
        divs['popup'].classList.add('fitw');
        return;
    }

    divs['popup'].classList.remove('fitw');

    popup_title.innerHTML = title;
    popup_body.innerHTML = text;
    popup_buttons.innerHTML = '';

    if (title.length === 0) {
        popup.classList.add('notitle');
    } else {
        popup.classList.remove('notitle');
    }

    popup_container.classList.remove('noevents');

    for (const button of buttons) {
        const button_class = button.class || 'neutral';
        const item = HTML(`<button class="${button_class}">${button.text.toUpperCase()}</button>`);
        
        if ('action' in button) {
            make_clickable(item, () => {
                const ret = button.action();
                if (ret !== false) {
                    popup_hide();
                }
            });
        } else {
            make_clickable(item, popup_hide);
        }

        popup_buttons.append(item);
    }

    popup_dimmer.classList.remove('hidden');
    divs['popup'].classList.remove('hidden');
}

function popup_hide() {
    popup_container.classList.add('noevents');
    popup_dimmer.classList.add('hidden');
    divs['popup'].classList.add('hidden');
}

let context_menu_hide_timer = null;

function context_menu_init() {
    divs['context-menu'].addEventListener('mouseleave', () => {
        context_menu_hide_timer = setTimeout(context_menu_hide, 300);
    })

    divs['context-menu'].addEventListener('mouseenter', () => {
        clearTimeout(context_menu_hide_timer);
    });

    if (touch_device) {
        divs['context-menu'].classList.add('bottom-full');
    }
}

// TODO: from bottom if close to bottom, else from top
// TODO: same for left/right
function context_menu_show(at, items) {
    divs['context-menu'].innerHTML = '';

    for (const item of items) {
        const menu_item = HTML(`<div class="item">${item.text}</div>`);
        make_clickable(menu_item, item.action);
        make_clickable(menu_item, context_menu_hide);
        divs['context-menu'].append(menu_item);
    }

    divs['context-menu'].classList.remove('hidden');

    const rect = divs['context-menu'].getBoundingClientRect();
    const parent = divs['main-page'].getBoundingClientRect();

    if (at.left + rect.width + 10 < parent.width) {
        divs['context-menu'].style.right = '';
        divs['context-menu'].style.left = at.left + 'px';
    } else {
        divs['context-menu'].style.left = '';
        divs['context-menu'].style.right = '10px';
    }
    
    if (at.top + rect.height + 10 < parent.height) {
        divs['context-menu'].style.bottom = '';
        divs['context-menu'].style.top = (at.top) + 'px';
    } else {
        divs['context-menu'].style.top = '';
        divs['context-menu'].style.bottom = '10px';
    }
}

function context_menu_show_bottom_full(items) {
    divs['context-menu'].innerHTML = '';

    for (const item of items) {
        const menu_item = HTML(`<div class="item">${item.text}</div>`);
        make_clickable(menu_item, item.action);
        make_clickable(menu_item, context_menu_hide);
        divs['context-menu'].append(menu_item);
    }

    divs['context-menu'].classList.remove('hidden');

    if (context_menu_touch_cancel_timer !== null) {
        clearTimeout(context_menu_touch_cancel_timer);
    }

    context_menu_touch_cancel = true;
    context_menu_touch_cancel_timer = setTimeout(() => {
        context_menu_touch_cancel = false;
    }, 500);
}

function context_menu_hide() {
    if (context_menu_touch_cancel) return;

    // CONTEXT_MENU_HIDE_TIMEOUT - this is a hack for touch devices so that context menu clicks
    // don't fall through to the elements under the context menu
    setTimeout(() => divs['context-menu'].classList.add('hidden'), CONTEXT_MENU_HIDE_TIMEOUT);
}
