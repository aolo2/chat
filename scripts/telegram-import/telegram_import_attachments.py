import os
import sys
import requests
import json

MEDIA_URL_BASE = 'https://bullet-chat.local/upload/'

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f'Usage: {sys.argv[0]} file_directory')
        sys.exit(1)

    print('TODO: CALCULATE FILE EXT LOCALLY')
    sys.exit(1)

    directory = sys.argv[1]

    result = []

    for file in os.listdir(directory):
        filename = os.fsdecode(file)
        fullpath = os.path.join(directory, filename)
        resp = requests.post(MEDIA_URL_BASE + 'upload-req')
        file_id = resp.headers['x-file-id']
        files = { 'file': open(fullpath, 'rb') }
        headers = {'X-File-Id': file_id}
        resp = requests.post(MEDIA_URL_BASE + 'upload-do', files=files, headers=headers)
        ext = resp.headers['x-file-ext']
        
        result.append({
            'filename': filename,
            'file_id': file_id,
            'ext': ext,
        })

    print(json.dumps(result, indent=4, ensure_ascii=False))
