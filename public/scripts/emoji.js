let emoji_list_show_timeout = null;
let emoji_list_hide_timeout = null;
let emoji_list_mode = 'message';
let emoji_list_body = null;
let emoji_histogram = null;
let emoji_wanted_field = null;

const EMOJI_SIZE = 32;
const EMOJI_MAX_FAVOURITES = 10;
const DEFAULT_EMOJIS = {
    "thumbs_up": { 'number_of_uses': 1, 'last_used': Date.now() },
    "-1": { 'number_of_uses': 1, 'last_used': Date.now() - 1 },
    "ok_hand": { 'number_of_uses': 1, 'last_used': Date.now() - 2 },
    "clap": { 'number_of_uses': 1, 'last_used': Date.now() - 3 },
    "pray": { 'number_of_uses': 1, 'last_used': Date.now() - 4 },
    "eyes": { 'number_of_uses': 1, 'last_used': Date.now() - 5 },
    "first_place_medal": { 'number_of_uses': 1, 'last_used': Date.now() - 6 },
    "hourglass": { 'number_of_uses': 1, 'last_used': Date.now() - 7 },
    "slightly_smiling_face": { 'number_of_uses': 1, 'last_used': Date.now() - 8 },
    "rage": { 'number_of_uses': 1, 'last_used': Date.now() - 9 }
};

function emoji_get_by_code(code) {
    let start = 0;

    for (const category of TWITTER_EMOJI) {
        const end = start + category.e.length;
        if (start <= code && code < end) {
            return category.e[code - start];
        }
        start += category.e.length;
    }

    return null;
}

function __emoji_get_code(s) {
    let code = 0;

    for (const category of TWITTER_EMOJI) {
        for (const emoji of category.e) {
            if (emoji.s === s) {
                return code;
            }
            ++code;
        }
    }
}

// TODO: @speed lookup by system name
function emoji_by_name(name) {
    if (name.length < 2) return null;

    const key = name.slice(1, -1);

    for (const category of TWITTER_EMOJI) {
        for (const emoji of category.e) {
            if (emoji.s === key) {
                return emoji;
            }
        }
    }

    return null;
}

function emoji_generate_classes(emoji_size) {
    let touch_bg_scale = '';

    if (touch_device) {
        if (emoji_size === 32) {
            touch_bg_scale = 'background-size: 969px 969px';
        } else if (emoji_size === 64) {
            touch_bg_scale = 'background-size: 1881px 1881px';
        }
    }

    // add your styles here
    let css =
`.emoji${emoji_size} {
    width: ${emoji_size}px;
    height: ${emoji_size}px;
    background-image: url("static/images/${emoji_size}.png");
    ${touch_bg_scale}
}\n`;

    for (const category of TWITTER_EMOJI) {
        for (const emoji of category.e) {
            let offset_x;
            let offset_y;

            if (touch_device){
                if (emoji_size === 32) {
                    offset_x = -(emoji.c[0] * 17);
                    offset_y = -(emoji.c[1] * 17);
                } else if (emoji_size === 64) {
                    offset_x = -(emoji.c[0] * 33);
                    offset_y = -(emoji.c[1] * 33);
                }
            } else {
                offset_x = -(1 + emoji.c[0] * (emoji_size + 2));
                offset_y = -(1 + emoji.c[1] * (emoji_size + 2));
            }
            css += `.emoji${emoji_size}.e${emoji.s} { background-position: ${offset_x}px ${offset_y}px; }\n`;
        }
    }

    const style = document.createElement('style');
    style.appendChild(document.createTextNode(css));
    document.head.appendChild(style);
}

function update_favourite() {
    const emoji_picker_size = (touch_device ? 64 : 32);
    emoji_histogram = JSON.parse(localStorage.getItem('bc-favourite-emojis') || '{}');
    const favourites = Object.entries(emoji_histogram)
        .sort((a, b) => b[1].number_of_uses - a[1].number_of_uses || b[1].last_used - a[1].last_used)
        .slice(0, EMOJI_MAX_FAVOURITES);

    let favourite_emoji_elems = '';
    let favourite_emoji_section = emoji_list_body.querySelector("div.emoji-group.favourite");
    if (favourite_emoji_section === null) return;
    if (favourites.length > 0) {
        favourite_emoji_section.classList.remove('dhide');
        for (const [key, _] of favourites) {
            const emoji = emoji_by_name(':' + key + ':');
            const code = __emoji_get_code(key);
            favourite_emoji_elems += `<div data-code="${code}" data-emid="${emoji.s}" class="one-emoji" title="${emoji.d}"><div class="emoji${emoji_picker_size} e${emoji.s}"></div></div>`;
        }

        let favourite_emojis_list = emoji_list_body.querySelector("#favourite-emojis-list");
        if (favourite_emojis_list !== null) {
            favourite_emojis_list.innerHTML = favourite_emoji_elems;
            favourite_emojis_list.querySelectorAll('.one-emoji').forEach(emoji_click_callback);
        }
    } else {
        favourite_emoji_section.classList.add('dhide');
    }
}

function emoji_click_callback(em) {
    make_clickable(em, () => {
        const key = em.getAttribute('data-emid')

        if (!(key in emoji_histogram)) {
            emoji_histogram[key] = { 'number_of_uses': 0, 'last_used': -1 };
        }

        ++emoji_histogram[key].number_of_uses;
        emoji_histogram[key].last_used = Date.now();

        localStorage.setItem('bc-favourite-emojis', JSON.stringify(emoji_histogram));

        if (emoji_list_mode === 'message') {
            const insert = ':' + key + ':';
            const message_input = emoji_wanted_field;
            if (message_input.value.length === 0) {
                message_input.value = insert;
            } else {
                const last_char = message_input.value[message_input.value.length - 1];
                if (last_char === ' ' || last_char === '\n') {
                    message_input.value += insert;
                } else {
                    message_input.value += ' ' + insert;
                }
            }
            if (!touch_device) message_input.focus();
        } else if (emoji_list_mode === 'reaction') {
            const code = +em.getAttribute('data-code');
            enqueue_reaction_add(ls_get_current_channel_id(), reacting_index, code);
            hide_reaction_window();
        }
    });
}

function show_emoji_list() {
    divs['emoji-list'].classList.remove('hidden');
    update_favourite();
}

function emoji_list_position_at_bottom(toggle) {
    let x = toggle.getBoundingClientRect().x;
    divs['emoji-list'].style.position = 'absolute';
    divs['emoji-list'].style.bottom = '60px';
    divs['emoji-list'].style.left = `${x - 10}px`;
    divs['emoji-list'].style.top = 'unset';
    divs['emoji-list'].style.right = 'unset';
}

function emoji_list_position_near_button(button, dir) {
    const at = button.getBoundingClientRect();
    const rect = divs['emoji-list'].getBoundingClientRect();
    const parent = divs['main-page'].getBoundingClientRect();

    divs['emoji-list'].style.position = 'fixed';

    if (dir === 'left') {
        at.x -= rect.width;
    }

    if (at.left + rect.width + 10 < parent.width) {
        divs['emoji-list'].style.right = 'unset';
        divs['emoji-list'].style.left = at.left + 'px';
    } else {
        divs['emoji-list'].style.left = 'unset';
        divs['emoji-list'].style.right = '10px';
    }

    if (at.top + rect.height + 10 < parent.height) {
        divs['emoji-list'].style.bottom = '';
        divs['emoji-list'].style.top = (at.top) + 'px';
    } else {
        divs['emoji-list'].style.top = '';
        divs['emoji-list'].style.bottom = '10px';
    }
}

function emoji_init() {
    emoji_list_body = find('emoji-list-body');

    // TODO: save favourites on the server?? (or else they get reset on relogin)

    emoji_generate_classes(16);
    emoji_generate_classes(32);

    if (touch_device) {
        emoji_generate_classes(64);
    }
    // window.addEventListener('keydown', (e) => {
    //     if (e.code === 'Escape') {
    //         emoji_list.classList.add('dhide');
    //     }
    //     return true;
    // });

    const emoji_picker_size = (touch_device ? 64 : 32);
    let picker_body = '';

    // TODO: fill favourites (with .code !)
    if (localStorage.getItem('bc-favourite-emojis') === null) {
        localStorage.setItem('bc-favourite-emojis', JSON.stringify(DEFAULT_EMOJIS));
    }
    emoji_histogram = JSON.parse(localStorage.getItem('bc-favourite-emojis'));
    const favourites = Object.entries(emoji_histogram)
        .sort((a, b) =>
                  b[1].number_of_uses - a[1].number_of_uses || b[1].last_used - a[1].last_used)
        .slice(0, EMOJI_MAX_FAVOURITES);

    picker_body += `<div class="emoji-group favourite${favourites.length === 0 ? ' dhide' : ''}"><div class="title">Frequently used</div><div class="emojis" id="favourite-emojis-list"></div></div>`

    let i = 0;
    
    for (const category of TWITTER_EMOJI) {
        picker_body += `<div class="emoji-group"><div class="title">${category.n}</div><div class="emojis">`;
        for (const emoji of category.e) {
            picker_body += `<div data-code="${i}" data-emid="${emoji.s}" class="one-emoji" title="${emoji.d}"><div class="emoji${emoji_picker_size} e${emoji.s}"></div></div>`;
            ++i;
        }
        picker_body += '</div></div>';
    }

    emoji_list_body.innerHTML = picker_body;
    emoji_list_body.querySelectorAll('.one-emoji').forEach(emoji_click_callback);

    update_favourite();

    add_toggle_emoji(divs['toggle-emoji-input'], divs['message-input']);

    divs['emoji-list'].addEventListener('mouseenter', () => {
        clear_timeouts();
    });

    divs['emoji-list'].addEventListener('mouseleave', () => {
        clear_timeouts();
        emoji_list_hide_timeout = setTimeout(() => {
            hide_reaction_window();
        }, 300);
    });
}

function clear_timeouts() {
    window.clearTimeout(emoji_list_hide_timeout);
    window.clearTimeout(emoji_list_show_timeout);
}

function toggle_reaction_window(index, button, dir = 'right') {
    if (index === reacting_index) {
        hide_reaction_window();
    } else {
        reacting_index = index;
        emoji_list_mode = 'reaction';

        if (!touch_device) {
            emoji_list_position_near_button(button, dir)
        } else {
            emoji_list_position_at_bottom(button);
        }

        show_emoji_list();
    }
}

function hide_reaction_window() {
    divs['emoji-list'].classList.add('hidden');
    reacting_index = null;
    emoji_list_mode = 'message';
}

function add_toggle_emoji(toggle, input_field) {
    toggle.addEventListener('mouseenter', () => {
        if (emoji_list_mode === 'message') {
            emoji_wanted_field = input_field;
            emoji_list_position_at_bottom(toggle);
            show_emoji_list();
            clear_timeouts();
        }
    });

    toggle.addEventListener('mouseleave', () => {
        clear_timeouts();
        if (emoji_list_mode === 'message') {
            emoji_list_hide_timeout = setTimeout(() => {
                hide_reaction_window();
            }, 300);
        }
    });
}