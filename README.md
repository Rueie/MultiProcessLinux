# Информация
Данная работа показывает как могут общаться между друг другом процессы в UNIX системах. Использовались PIPE-ы, shared memory и socket-ы. <br>
Смысл закючался в том, что главный процесс порождает дочерние и передает им задачи на исполнение, а они присылают ответ: <br>
* workWithPipes() - взаимодействие через pipe-ы
* workWithSockets() - взаимодействие через сокеты
* workTiwhSharedMemory() - взаимодействие через разделяемую память
