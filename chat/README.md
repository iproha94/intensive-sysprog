Done:
* динамическая очередь сообщений
* динамический список контекстов
* очередь сообщений общая для всех клиентов
* новому клиенту приходят сообщения, пришедшие после его ассепта
* сообщения, которые отправились всем получателям - удаляются, даже если отправка сообщений будет в несколько этапов

Todo:
* в цикле чтение с клиента, запись в клиента, ассепт на серверном сокете
* вынести работу с каждой абстрацией в хедеры
* проследить чтобы в строке было не больше 80 символов, да и вообще си стайл соблюсти