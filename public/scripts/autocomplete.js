const MAX_AUTOCOMPLETE_ITEMS = 10;

function autocomplete_set_on_element(element, variants_callback) {
    // NOTE(aolo2): all functions are implemented as closures, so the state
    // is stored in the local variables below
    let list_item = null;
    let current_context = [];
    let active = null;
    let active_item = null;

    list_item = HTML(`<div class="autocomplete-items vhide"></div>`);
    element.parentNode.insertBefore(list_item, element);

    function on_input(e) {
        if (e.data === null && active === null) {
            // If this was a deletion, and not an input, then don't autocomplete
            return;
        }

        const words = element.value.split(' ');
        const last_word = words[words.length - 1];
        
        current_context = variants_callback(last_word).slice(0, MAX_AUTOCOMPLETE_ITEMS);

        if (current_context.length === 0) {
            active = null;
            list_item.classList.add('vhide');
            return;
        }

        list_item.innerHTML = '';

        for (let i = 0; i < current_context.length; ++i) {
            const item = create_autocomplete_item(i);
            list_item.appendChild(item);
        }
        
        set_active_item(0); // Highlight the first autocomplete variant by default (that's what people generally do in other apps: Discord, Sublime Text)

        list_item.classList.remove('vhide');
    }

    function on_keydown(e) {
        if (active !== null) {
            if (e.code === 'Escape') {
                list_item.classList.add('vhide');
                active = null;
            } else if ((e.code === 'Enter' && !e.shiftKey) || e.code === 'Tab') {
                perform_autocomplete(active);
                active = null;
                list_item.classList.add('vhide');
                e.preventDefault();
                return true;
            } else if (e.code === 'ArrowDown') {
                set_active_item(active + 1);
                e.preventDefault();
                return true;
            } else if (e.code === 'ArrowUp') {
                set_active_item(active - 1);
                e.preventDefault();
                return true;
            }

            return false;
        }
    }

    function create_autocomplete_item(index) {   
        const variant = current_context[index];
        const item = HTML(`<div class="autocomplete-item" data-autocomplete-index="${index}">
            <span class="autocomplete-item-left">${variant.content_left}</span>
            <span class="autocomplete-item-right">${variant.content_right}</span>
        </div>`);

        make_clickable(item, () => {
            perform_autocomplete(index)
            list_item.classList.add('vhide');
            active = null;
        });

        item.addEventListener('mouseover', () => {
            active = index;
            set_active_item(index);
        });

        return item;
    }

    function set_active_item(index) {
        active = index;

        if (active_item) {
            active_item.classList.remove('active');
        }

        if (active === null) {
            return;
        }

        if (active >= current_context.length) {
            active = 0;
        }

        if (active < 0) {
            active = current_context.length - 1;
        }

        active_item = list_item.children.item(active);
        active_item.classList.add('active');
    }

    function perform_autocomplete(index) {
        let replace_from = 0;

        for (let i = element.value.length - 1; i >= 0; --i) {
            const c = element.value[i];
            if (/\s/.test(c)) {
                replace_from = i + 1;
                break;
            }
        }
        
        const replace_with = current_context[index].text;

        if (replace_from === 0) {
            element.value = replace_with;
        } else {
            element.value = element.value.slice(0, replace_from) + replace_with;
        }
    }

    // Turn off builtin autocomplete
    element.setAttribute('autocomplete', 'off');

    element.addEventListener('input', on_input);

    // parent.addEventListener('click', clear_autocomplete);

    return on_keydown;
}
