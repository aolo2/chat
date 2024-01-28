const months = [
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
];

function month_string(date) {
    return months[date.getMonth()];
}

function find(id) {
    return document.getElementById(id);
}

function findchild(parent, selector) {
    return parent.querySelector(selector);
}

function random_string() {
    return (Math.random() + 1).toString(36).substring(7);
}

function same_day(date_a, date_b) {
    if ((date_a === null && date_b !== null) || (date_a !== null && date_b === null)) {
        return false;
    }
    
    return date_a.getFullYear() === date_b.getFullYear()
        && date_a.getMonth() === date_b.getMonth() 
        && date_a.getDate() === date_b.getDate();
}

function HTML(html) {
    const template = document.createElement('template');
    template.innerHTML = html.trim();
    return template.content.firstChild;
}

function asyncify(func) {
    return async (...params) => {
        return new Promise((resolve, reject) => { 
            try {
                func(resolve, ...params); 
            } catch (e) {
                reject(e);
            }
        });
    };
}

function upTree(item, selector) {
    for ( ; item && item !== document; item = item.parentNode ) {
        if (item.matches(selector)) {
            return item;
        }
    }
    return null;
}

function is_supported_image(type) {
    return [ATTACHMENT_TYPE.EXT_IMAGE, ATTACHMENT_TYPE.EXT_JPEG, ATTACHMENT_TYPE.EXT_PNG, ATTACHMENT_TYPE.EXT_GIF].includes(type);
}

// rects must have "width" and "height"
function pack_images(rects) {
    const by_width = rects.sort(r => r.width);
    const by_height = rects.sort(r => r.height);
}

function has_mentions(text) {
    const me = ls_get_me();
    return text.includes('@' + me.login);
}

function highlight_mentions(text) {
    const current_channel_id = ls_get_current_channel_id();
    const current_channel = ls_get_channel(current_channel_id);
    const me = ls_get_me();

    const users = [];

    if (current_channel) {
        for (const user_info of current_channel.users) {
            const user = ls_find_user(user_info.id);
            if (user) {
                if (user.login === me.login) {
                    text = text.replaceAll('@' + user.login, `<span class="mention me">${user.name}</span>`);
                } else {
                    text = text.replaceAll('@' + user.login, `<span class="mention">${user.name}</span>`);
                }
            }
        }
    }

    return text;
}

function escape_html(text) {
     return text
         .replace(/&/g, '&amp;')
         .replace(/</g, '&lt;')
         .replace(/>/g, '&gt;')
         .replace(/"/g, '&quot;')
         .replace(/'/g, '&#039;');
}

function process_message_text(input) {
    let nonemoji_length = input.length;
    let final_text = input;

    for (const c of input) {
        if (c === ' ') {
            nonemoji_length -= 1;
        }
    }    

    final_text = escape_html(final_text);
    final_text = highlight_mentions(final_text);

    let listing_from = -1;
    let code_ranges = [];

    for (let i = 0; i < final_text.length; ++i) {
        if (final_text[i] === '`') {
            if (listing_from === -1) {
                listing_from = i;
            } else {
                code_ranges.push({'from': listing_from, 'to': i});
                listing_from = -1;
            }
        }
    }

    let did = true;
    while (did) {
        did = false;

        let at_http = -1;
        let at_https = -1;

        if (final_text.startsWith('http://')) {
            at_http = 0;
        }

        if (at_http === -1) {
            at_http = final_text.indexOf(' http://');
            if (at_http !== -1) ++at_http;
        }

        if (at_http === -1) {
            at_http = final_text.indexOf('\nhttp://');
            if (at_http !== -1) ++at_http;
        }

        if (final_text.startsWith('https://')) {
            at_https = 0;
        }

        if (at_https === -1) {
            at_https = final_text.indexOf(' https://');
            if (at_https !== -1) ++at_https;
        }

        if (at_https === -1) {
            at_https = final_text.indexOf('\nhttps://');
            if (at_https !== -1) ++at_https;
        }


        if (at_http !== -1 || at_https !== -1) {
            let at = -1;

            if (at_http !== -1) {
                at = at_http;
            } else {
                at = at_https;
            }

            for (let range of code_ranges) {
                if (range.from <= at && at < range.to) {
                    at = -1;
                }
            }   

            if (at !== -1) {
                did = true;

                let to = final_text.length;

                for (let i = at + 1; i < final_text.length; ++i) {
                    if (final_text[i] === ' ' || final_text[i] === '\n') {
                        to = i;
                        break;
                    }
                }

                const pre = final_text.substring(0, at);
                const it = `<a target="_blank" href="${final_text.substring(at, to)}">${final_text.substring(at, to)}</a>`;
                const post = final_text.substring(to);

                // console.log('pre', pre)
                // console.log('it', it)
                // console.log('post', post)

                final_text = pre + it + post;
            }
        }
    }

    if (code_ranges.length > 0) {
        // console.log(code_ranges);
        let new_final_text = '';
        let from = 0;

        for (const range of code_ranges) {
            new_final_text += final_text.substring(from, range.from);
            new_final_text += '<code>';
            new_final_text += final_text.substring(range.from + 1, range.to);
            // console.log(final_text.substring(range.from + 1, range.to))
            new_final_text += '</code>';
            from = range.to + 1;
        }

        if (code_ranges[code_ranges.length - 1].to + 1 !== final_text.length) {
            new_final_text += final_text.substring(code_ranges[code_ranges.length - 1].to + 1);
        }

        final_text = new_final_text;
    }

    final_text = final_text.replace(/\n/g, '<br>');


    let emoji_count = 0;
    const inline_emoji_size = (touch_device ? 32 : 16);
    did = true;
    while (did) {
        did = false;
        for (let i = 0; i < final_text.length; ++i) {
            if (final_text[i] === ':') {
                const at = i;

                ++i;

                while (i < final_text.length && final_text[i] != ':') {
                    ++i;
                }

                if (i < final_text.length) {
                    const key = final_text.substring(at, i + 1);
                    const emoji = emoji_by_name(key);
                    if (emoji) {
                        nonemoji_length -= key.length;
                        emoji_count += 1;
                        final_text = final_text.replace(key, `<span class="emoji${inline_emoji_size} e${emoji.s} message-inline-emoji"></span>`);
                        did = true;
                        break;
                    }
                }
            }
        }
    }

    const emojied = (nonemoji_length === 0 && 0 <= emoji_count && emoji_count <= 3);

    return {'text': final_text, 'emojied': emojied};
}

function get_direct_partner(channel) {
    if (channel.is_dm) {
        let other_person = null;

        const me = ls_get_me();
        for (const user of channel.users) {
            if (user.id !== me.user_id) {
                other_person = user.id;
                break;
            }
        }

        if (other_person !== null) {
            const them = ls_find_user(other_person);
            return them;
        } else {
            return ls_find_user(me.user_id);
        }
    }

    return null;
}

function get_channel_avatar(channel, bigger=false) {
    // TODO: channel avatars (get from special kind of record)
    let avatar = 'static/images/ph.svg';

    if (channel.is_dm) {
        const them = get_direct_partner(channel);
        if (them && them.avatar !== '0') {
            avatar = `/storage/${them.avatar}-${bigger ? '2' : '3'}`;
        } else {
            // TODO: generate svg of the first letter
        }
    } else {
        if (channel.avatar_id !== '0') {
            avatar = `/storage/${channel.avatar_id}-${bigger ? '2' : '3'}`;
        }
    }

    return avatar;
}

function user_status_string(user) {
    let status_string = '';

    switch (user.status) {
        case USER_STATUS.OFFLINE: {
            status_string = 'offline';
            break;
        }

        case USER_STATUS.ONLINE: {
            status_string = 'online';
            break;
        }

        case USER_STATUS.AWAY: {
            status_string = 'away';
            break;
        }

        case USER_STATUS.BUSY: {
            status_string = 'busy';
            break;
        }

        default: {
            status_string = 'offline';
            break;
        }
    }

    return status_string;
}

function get_timestamp() {
    return Math.floor(Date.now()/1000);
}

function make_clickable(item, listener) {
    // click for the mouse, tap for the touch devices
    if (touch_device) {
        make_button(item, listener);
    } else {
        item.addEventListener('click', listener);
    }
}
