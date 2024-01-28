import json
import sys
import base64

def convert_channel(channel, attachments_list):
    channel_title = channel['name']
    messages = []
    users = {}
    attachments = {}

    fields = {}
    id_mapping = {}
    counter = 0

    for attachment in attachments_list:
        attachments[attachment['filename']] = attachment

    for item in channel['messages']:
        if item['type'] != 'message':
            continue

        message = {}

        id_mapping[item['id']] = counter
        counter += 1

        for key in item:
            if key not in fields:
                fields[key] = item[key]

        if item['from_id'] not in users:
            users[item['from_id']] = item['from']

        if type(item['text']) == str:
            message['text'] = item['text']
        elif type(item['text']) == list:
            text = ""

            for text_item in item['text']:
                if type(text_item) == str:
                    text += text_item
                elif type(text_item) == dict:
                    if text_item['type'] == 'code':
                        text += '`' + text_item['text'] + '`'
                    else:
                        text += text_item['text']

            message['text'] = text

        message['id'] = item['id']
        message['type'] = 'text'
        message['author'] = item['from_id']
        message['timestamp'] = int(item['date_unixtime'])

        messages.append(message)

        if len(item['text']) == 0:
            if 'file' in item:
                message['text'] = '[attachment not migrated]'
            else:
                message['text'] = ''

        if 'photo' in item:
            filename = item['photo'].split('/')[-1]
            attach = attachments[filename]
            generated = {
                'id': item['id'],
                'type': 'attach',
                'fileid': attach['file_id'],
                'filename': filename,
                'ext': attach['ext'],
            }
            messages.append(generated)
            counter += 1

        if 'reply_to_message_id' in item:
            generated = {
                'id': item['id'],
                'type': 'reply',
                'reply_to': item['reply_to_message_id'],
            }
            messages.append(generated)
            counter += 1


    for message in messages:
        message['id'] = id_mapping[message['id']]
        if 'reply_to' in message:
            if message['reply_to'] in id_mapping:
                message['reply_to'] = id_mapping[message['reply_to']] 
            else:
                message['reply_to'] = 0

    # print(json.dumps(users, ensure_ascii=False))
    # print(json.dumps(messages, ensure_ascii=False))

    print(channel_title)
    print(len(users))
    # print(len(attachemnts))
    print(len(messages))

    for user_id in users:
        print(user_id, users[user_id])

    for message in messages:
        if message['type'] == 'text':
            text = base64.b64encode(message['text'].encode('utf-8')).decode('ascii')
            print(message['id'], message['type'], message['timestamp'], message['author'], text)
        elif message['type'] == 'attach':
            print(message['id'], message['type'], message['ext'], message['fileid'], message['filename'])
        elif message['type'] == 'reply':
            print(message['id'], message['type'], message['reply_to'])


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} attachments.json messages.json')
        sys.exit(1)

    attachments_filename = sys.argv[1]
    messages_filename = sys.argv[2]

    f1 = open(messages_filename, 'rb')
    channel = json.loads(f1.read())

    f2 = open(attachments_filename, 'rb')
    attachments = json.loads(f2.read())

    convert_channel(channel, attachments)