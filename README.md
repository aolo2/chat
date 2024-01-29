# Bullet.Chat
This an initial opensource release of an internal chatting application I have developed, which we call Bullet.Chat (as a reference to Rocket.Chat). This repository contains:
* A web-based chat frontend for desktop and mobile. Both clients use the same code with slightly different css. The mobile version can also be installed as a PWA (code is in `public`)
* A websocket messaging server (code in `src/websocket`)
* A file upload / image preview generation server (code in `src/media`)
* A (95% complete) notification server, which creates push notifications via Push API (code in `src/notification`)
* Some shared system-level code for doing disk persistence, keeping track of io_uring request metadata, creating growable buffers etc (code in `src/shared`)
* Python scripts to convert exported Telegram chats into a format that can easily be imported into the database (code in `telegram-import`)
* A set of build scripts for building debug (dynamically linked) and release (statically linked with musl) binaries (code in `scripts`)

## Dependencies
The frontend uses no dependencies. The `public` folder contains everything you need.

To *build* the toolchain and the backend you need `gcc g++ make cmake wget`.

To *run* the backend in **debug** mode, you'll need `liburing libcrypt` from your package manager. To *run* the backend in **release** mode you don't need anything, it's built statically using the downloaded toolchain.

The backend uses io_uring functions that are present since Linux 5.10 (released Dec 2020), so make sure you're on a new enough kernel.

## Building the toolchain
Download and build the toolchain by running the provided script:
```bash
./scripts/000-build-toolchain.sh
```
This takes quite some time, because it builds OpenSSL and Curl from source. You only need to run this once though.

## Building the backend
To build debug (dynamically linked, no optimizations, with debug symbols) binaries, call these scripts:
```bash
./scripts/001-build-websocket-debug.sh
./scripts/003-build-media-debug.sh
./scripts/005-build-notification-debug.sh
```
You should now see the executables in the `build` folder.

To build release (statically linked with musl, no optimizations, with debug symbols) binaries, call these scripts:
```bash
./scripts/002-build-websocket-release.sh
./scripts/004-build-media-release.sh
./scripts/006-build-notification-release.sh
```
The executables should be in the `build` folder as well.

## Running the backend
The instructions for the release and debug versions of the backend are the same, so I'll give snippets for the release version. Just substitute the binary filename if you want to run the debug exes.

To run the websocket server on port `7271` using the folder `data` for the database:
```bash
./build/ws-server-release 7271 data/ --dev
```

To run the media server on port `7000` using the folder `uploads` for the uploaded files (make sure to create the folder if it doesn't exist yet):
```bash
./build/media-server-release 7000 uploads/ --dev
```

The notitication server is not connected to anything at this moment, so there's no point in running it.

Finally, start caddy with the provided example config to host the chat on `https://localhost`:
```bash
/path/to/caddy_binary run --config config/Caddyfile
```

## Setting up
Warning, this is a bit [grug brain](https://grugbrain.dev/) at the moment!

If you successfully built and ran the backend, you should be able to open https://localhost in your web browser and see (after going past the "your certificate is not valid" screen) the login screen:
![image](https://github.com/aolo2/chat/assets/17429989/170e74a5-fc5b-4073-834f-383a094dc6ca)

You won't be able to log in though since there are no users. To create a user, go to the AAP (Advanced Admin Panel) at https://localhost/admin.html, and enter the desired login, password and display name. After that click "add user" and go back to the login page. Now you should be able to login and see a mostly empty page:
![image](https://github.com/aolo2/chat/assets/17429989/90e5802b-a875-4a3a-af4f-a4fa953f5bc4)

Finally, create a channel by clicking "Channel", entering the channel title, and clicking "create". The setup is done!
![image](https://github.com/aolo2/chat/assets/17429989/af2dd8dc-1050-4a58-9903-741c0bd40d9c)

You can add more users from the admin panel. Invite the new users to the channel by clicking on the "people" icon in the top right. Note that the chat currently can't properly handle being opened from multiple browser tabs at the same time.
