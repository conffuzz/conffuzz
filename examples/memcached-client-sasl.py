import bmemcached
client = bmemcached.Client(('127.0.0.1:11211', ), 'root@2a46ea8ec91e',
                                    '1234')

for i in range(10000):
    client.set('key' + str(i), 'value' + str(i))

for i in range(10000):
    print(client.get('key' + str(i)))
