# Bullet.Chat
This an initial opensource release of an internal chatting application I have developed, which we call Bullet.Chat (as a reference to Rocket.Chat). This repository contains:
* A web-based chat frontend for desktop and mobile. Both clients use the same code with slightly different css. The mobile version can also be installed as a PWA (code is in `public`)
* A websocket messaging server (code in `src/websocket`)
* A file upload / image preview generation server (code in `src/media`)
* A (95% complete) notification server, which creates push notifications via Push API (code in `src/notification`)
* Some shared system-level code for doing disk persistence, keeping track of io_uring request metadata, creating growable buffers etc (code in `src/shared`)
* Python scripts to convert exported Telegram chats into a format that can easily be imported into the database (code in `telegram-import`)
* A set of build scripts for building debug (dynamically linked) and release (statically linked with musl) binaries (code in `scripts`)
