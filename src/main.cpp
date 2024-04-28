#include "gauss.h"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <math.h>
#include <map>
#include <string>
#include "sys/types.h"
#include "sys/socket.h"
#include <sys/mman.h>

///
/// \brief Подготавливает сообщение к отправке, дополняя его номером
/// \param[in] task_number Номер задачи
/// \param[in] data Данные
/// \param[in] data_size Размер данных
/// \param[out] message Сообщение с его номером в начале
/// \param[out] message_size Размер итогового сообщения
/// \note Не забудьте освободить память, выделенную под упакованное сообщение!
///
void compressMessageForSending(unsigned int task_number, char* data, unsigned int data_size, char* &message, long long *message_size){
    *message_size = sizeof(long long) + sizeof(int) + data_size;
    message = new char[*message_size];
    memcpy(message, message_size, sizeof(long long));
    memcpy(message + sizeof(long long), &task_number, sizeof(int));
    memcpy(message + sizeof(long long) + sizeof(int), data, data_size);
}

///
/// \brief Извлекает номер сообщение и сами данные
/// \param[in] message Сообщение
/// \param[out] task_number Номер задачи
/// \param[out] data Само сообщение
/// \note Не забудьте освободить память, выделенную под содержимое сообщения!
///
void extractMessageFromSending(char* message, unsigned int *task_number, char *&data){
    long long message_size;
    memcpy(&message_size, message, sizeof(long long));
    memcpy(task_number, message + sizeof(long long), sizeof(int));
    data = new char[message_size - sizeof(long long) - sizeof(int)];
    memcpy(data, message + sizeof(long long) + sizeof(int), message_size - sizeof(long long) - sizeof(int));
}

///
/// \brief Ошибки процессов
///
enum class ProcessError{
    NoWorkerNumber = 1,     /// < Не удалось получить номер работника
    PipeSendingError,       /// < Не удалось отправить результат расчётов
    ErrorListen             /// < Не удалось начать слушание соединений
};

///
/// \brief Преобразует номер ошибки в строковый формат
/// \param[in] error Номер ошибки
/// \return Ошибка в виде строки
///
string processErrorToString(ProcessError error){
    string result;
    switch (error) {
    case ProcessError::NoWorkerNumber:
        result = "Не удалось получить номер работника";
        break;
    case ProcessError::PipeSendingError:
        result = "Не удалось отправить результат расчётов";
        break;
    case ProcessError::ErrorListen:
        result = "Не удалось начать слушание соединений";
        break;
    default:
        result = "Неизвестная ошибка";
        break;
    }
    return result;
}

void workWithPipes(){
    //  Время ожидания задач на дочернем процессе (в сек)
    int seconds_wait_tasks = 3;
    // Максимальный размер чтения данных из памяти
    int MAX_MSG_SIZE = 2048;

    int MAX_MATRIX_SIZE = floor(sqrt(static_cast<double>(MAX_MSG_SIZE - sizeof(int) * 2 - sizeof(long long) // long long int - под длину сообщения, один int под номер задачи, второй - под размер матрицы (см. конвертацию матрицы в char*)
                                                         ) / sizeof(double)));      //  Округление в меньшую сторону

    unsigned int    number_workers,
                    number_matrix,
                    matrix_size;
    double  max,
            min;

    //  Получение данных от пользователя
    cout << "Введите число работников: дочерних процессов: ";
    cin >> number_workers;
    cout << "Введите число матриц, которые вы хотите сгенерировать: ";
    cin >> number_matrix;
    cout << "Введите размерность матрицы (максимальный размер матрицы при максимальном размере передаваемого сообщения (" << MAX_MSG_SIZE << ") равен " << MAX_MATRIX_SIZE << "): ";
    cin >> matrix_size;
    if (matrix_size > MAX_MATRIX_SIZE){
        cout << "Введён размер матрицы, который превышает максимальный допустимый! Для увеличения - измените максимальный размер передаваемого сообщения!\n";
        return;
    }
    cout << "Введите диапазон значений, в пределах которых вы хотите генерировать значения матрицы: ";
    cin >> min >> max;
    cout << "-----info-----\n";
    cout    << "Печать введённых данных:\n"
            << "Число работников (дочерних процессов): " << number_workers << endl
            << "Число гененрируемых матриц: " << number_matrix << endl
            << "Интервал генерируемых значений матрицы: [" << min << "; " << max << "]\n";
    cout << "-----info-----\n";

    pid_t *workers_pid = new pid_t[number_workers];
    int *status = new int[number_workers];
    // Статус -1 - процесс не закончился, любое отличное значение - завершился (0 - удача, больше 0 - ошибка)
    memset(status, -1, sizeof(int) * number_workers);

    int to_main[2];                 //  Pipe в главные процесс
    int from_main_with_workers[2];    //  Pipe из главного процесса с номерами работников, которые они забирают сами
    vector<int*> pipes_to_child{};  //  Список pipe-ов в дочерние процессы

    if (pipe(to_main) == -1){   //  Создаем pipe
        cout << "Ошибка создания потока в родителя!\n";
        return;
    }


    //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
    const int flags_to_main = fcntl(to_main[0], F_GETFL, 0);
    fcntl(to_main[0], F_SETFL, flags_to_main | O_NONBLOCK);

    if (pipe(from_main_with_workers) == -1){  //  Создаем pipe
        cout << "Ошибка создания потока из родителя с номерами работников!\n";
        close(to_main[0]);
        close(to_main[1]);
        return;
    }

    //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
    const int flags_from_main = fcntl(from_main_with_workers[0], F_GETFL, 0);
    fcntl(from_main_with_workers[0], F_SETFL, flags_from_main | O_NONBLOCK);

    //  Открываем pipe-ы для дочерних процессов
    for(auto i {0}; i < number_workers; i++){
        pipes_to_child.push_back(new int[2]);
        if(pipe(pipes_to_child[i]) == -1){  //  Если не удалось открыть поток, то завершаем работу
            cout << "Ошибка создания потока №" << i << " в дочерние процессы!\n";
            close(to_main[0]);
            close(to_main[1]);
            close(from_main_with_workers[0]);
            close(from_main_with_workers[1]);
            for(auto j {i}; j != 0; j--){
                close(pipes_to_child[j][0]);
                close(pipes_to_child[j][1]);
            }
            return;
        }
        //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
        const int flags = fcntl(pipes_to_child[i][0], F_GETFL, 0);
        fcntl(pipes_to_child[i][0], F_SETFL, flags | O_NONBLOCK);
    }

    //  Выдаём номер работника в общий pipe
    for(int i {0}; i < number_workers; i++){
        write(from_main_with_workers[1], &i, sizeof(int));
    }

    char    *matrix_buf,    //  Буфер для матриц
            *message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
    unsigned int data_size;          //  Размер матрицы в формате char*
    long long message_size; //  Размер передаваемого сообщения

    //  Создаём дочерние процессы
    for(unsigned int i {0}; i < number_workers; i++){
        switch (workers_pid[i] = fork()) {
        case -1:{
            cout << "Ошибка создания worker-а!\n";
            exit(1);
        }break;
        case 0:{//Это дочерний процесс
            ///
            /// \brief Функциональный объект для печати
            ///
            class PrintChildInfo {
            public:
                ///
                /// \brief Печатает информацию о процессе
                /// \param[in] str Печатоемое сообщение
                ///
                void operator ()(const string& str){
                    cout    << "->Это дочерний процесс (" << getpid() << "), мой родитель (" << getppid() << "):\n"
                            << "-->" << str << endl;
                }
            };
            PrintChildInfo printChildInfo;  //  Функциональный объект для печати инфы о дочернем процессе (сделано так, т.к. если сделать её просто функцией в глобальной области видимости, то pid родителя будет равен 1!)
            int worker_number,
                number_try_to_read {0},     //  Число неудачных попыток чтения
                max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения
            unsigned int local_data_size;          //  Размер матрицы в формате char*
            long long local_message_size; //  Размер передаваемого сообщения

            char    *local_matrix_buf,           //  Буфер для матриц
                    *local_message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
            local_message_buf = new char[MAX_MSG_SIZE];
            local_matrix_buf = new char[MAX_MSG_SIZE];
            bool success {false};   //  Флаг удачи
            printChildInfo("Считываю свой номер работника...");
            while(number_try_to_read != max_number_of_read_fails){  //  Считывваем свой номер работника, чтобы определить поток, из которого будем потом читать
                if(read(from_main_with_workers[0], &worker_number, sizeof(int)) <= 0){
                    number_try_to_read++;
                    printChildInfo("Не удалось определить свой номер! Осталось попыток на чтение (" + to_string(max_number_of_read_fails - number_try_to_read) + ")! Пытаюсь снова считать свой номер работника...");
                } else {
                    success = true;
                    break;
                }
                sleep(1);
            }
            if (!success){
                printChildInfo("Не смог получить свой рабочий номер! Завершаю свою работу!");
                exit(1);    // Не смогли получить свой номер
            }
            printChildInfo("Мой рабочий номер: " + to_string(worker_number));
            printChildInfo("Ожидаю задачи (" + to_string(seconds_wait_tasks) + " секунд(ы) буду ждать задачи, затем завершу свою работу)!");
            memset(local_matrix_buf, 0, MAX_MSG_SIZE);
            memset(local_message_buf, 0, MAX_MSG_SIZE);
            number_try_to_read = 0;
            while(number_try_to_read != max_number_of_read_fails){
                if(read(pipes_to_child[worker_number][0], local_message_buf, sizeof(long long)) > 0){
                    number_try_to_read = 0;
                    memcpy(&local_message_size, local_message_buf, sizeof(long long));
                    printChildInfo("Получил сообщение длинной <" + to_string(local_message_size) + "> байт\n");
                    read(pipes_to_child[worker_number][0], local_message_buf + sizeof(long long), local_message_size - sizeof(long long));
                    unsigned int task_number;   //  Номер задачи
                    extractMessageFromSending(local_message_buf, &task_number, local_matrix_buf);
                    memset(local_message_buf, 0, local_message_size);
                    square_matrix tmp_matrix = fromByteToMatrix(local_matrix_buf);
                    memset(local_matrix_buf, 0, local_message_size - sizeof(long long) - sizeof(int));
                    printChildInfo("Получили задачу номер <" + to_string(task_number) + ">. Её содержимое:");
                    printMatrix(tmp_matrix);
                    tmp_matrix = findReverseMatrix(tmp_matrix);
                    printChildInfo("Обратная матрица к полученной:");
                    printMatrix(tmp_matrix);
                    fromMatrixToByte(tmp_matrix, local_matrix_buf, &local_data_size);
                    compressMessageForSending(task_number, local_matrix_buf, local_data_size, local_message_buf, &local_message_size);
                    int local_number_try_to_read {0};
                    while (local_number_try_to_read != max_number_of_read_fails){
                        printChildInfo("Отправка результата решения задачи номер <" + to_string(task_number) + "> в родительский процесс...");
                        if (write(to_main[1], local_message_buf, local_message_size) == -1){
                            local_number_try_to_read++;
                            printChildInfo("Ошибка отправки решения родителю. Осталось попыток для отправки (" + to_string(max_number_of_read_fails - local_number_try_to_read) + "). Повторная попытка отправки данных...");
                            sleep(1);
                        } else {
                            success = true;
                            break;
                        }
                    }
                    if (!success){
                        printChildInfo("Завершаю свою работу, т.к. я не могу отправлять сообщения в родителя!");
                        exit(2);    //  Не удалось отправить результат расчётов
                    }
                    printChildInfo("Решение задачи номер <" + to_string(task_number) + "> удачно отправлено на родителя! Ожидаю новые задачи...");
                } else{
                    number_try_to_read++;
                    printChildInfo("Не получил задачи в течении " + to_string(seconds_wait_tasks) + " секунд(ы). Попыток получить задачу перед завершением (" + to_string(max_number_of_read_fails - number_try_to_read) + ")...");
                    sleep(seconds_wait_tasks);
                }
            }
            printChildInfo("Завершаю свою работу!");
            delete[] local_matrix_buf;
            delete[] local_message_buf;
            exit(0);
        }break;
        default://Это родительский процесс
            break;
        }
    }

    map<unsigned int, square_matrix>    tasks,          //  Сгенерированные задачи
                                        tasks_result;   //  Полученные ответы на задачи

    //  Создаём задачи
    for(auto i {0}; i < number_matrix; i++){
        tasks[i] = genMatrix(matrix_size, min, max);
        cout << "Задача номер " << i << ":\n";
        printMatrix(tasks[i]);
    }

    //  Отправляем задачи
    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);
    for(const auto& task: tasks){
        fromMatrixToByte(task.second, matrix_buf, &data_size);
        compressMessageForSending(task.first, matrix_buf, data_size, message_buf, &message_size);
        memset(matrix_buf, 0, data_size);
        write(pipes_to_child[task.first % number_workers][1], message_buf, message_size);
        memset(message_buf, 0, message_size);
    }

    //  Получаем ответы
    int number_try_to_read {0},     //  Число неудачных попыток чтения
        max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения
    delete[] message_buf;
    delete[] matrix_buf;
    message_buf = nullptr;
    matrix_buf = nullptr;
    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);
    while(number_try_to_read != max_number_of_read_fails){
        cout << "Родитель: Ожидание результатов от дочерних процессов...\n";
        if (read(to_main[0], message_buf, sizeof(long long)) > 0){
            cout << "Родитель: получил данные:\n";
            number_try_to_read = 0;
            memcpy(&message_size, message_buf, sizeof(long long));
            read(to_main[0], message_buf + sizeof(long long), message_size - sizeof(long long));
            unsigned int task_number;
            extractMessageFromSending(message_buf, &task_number, matrix_buf);
            tasks_result[task_number] = fromByteToMatrix(matrix_buf);
            memset(matrix_buf, 0, message_size - sizeof(long long) - sizeof(int));
            memset(message_buf, 0, message_size);
            cout << "Ответ на задачу номер <" << task_number << ">:\n";
            printMatrix(tasks_result[task_number]);
        } else{
            number_try_to_read++;
            cout << "Не получил решние задач! Число попыток проверки наличия задач <" << max_number_of_read_fails - number_try_to_read << ">.Ожидаю решения задач...\n";
            sleep(seconds_wait_tasks * 2);
        }
    }


    //  Тут ждём завершение процессов-worker-ов
    for(int i {0}; i < number_workers; i++){
        waitpid(workers_pid[i], &status[i], 0);
        switch (status[i]) {
        case 0:
            cout << "Процесс-worker (" << workers_pid[i] << ") завершился удачно\n";
            break;
        default:
            cout << "Процесс-worker (" << workers_pid[i] << ") завершился с ошибкой! Код ошибки: < " << status[i] << "> (" << processErrorToString(static_cast<ProcessError>(status[i])) << ")\n";
            break;
        }
    }
    cout << "Дочерние процессы выключились!\n";

    delete[] workers_pid;
    delete[] status;
    delete[] matrix_buf;
    delete[] message_buf;

    //  Печать решения задач
    for(auto i {0}; i < number_matrix; i++){
        cout << "Задача номер <" << i << ">:\n";
        printMatrix(tasks[i]);
        cout << "Её решение:\n";
        if (tasks_result.count(i)){
            printMatrix(tasks_result[i]);
        } else {
            cout << "Нет решения!\n";
        }
    }

    close(from_main_with_workers[0]);
    close(from_main_with_workers[1]);
    close(to_main[0]);
    close(to_main[1]);

    for(auto i {0}; i != number_workers; i++){
        close(pipes_to_child[i][0]);
        close(pipes_to_child[i][1]);
    }
}

///
/// \brief Полное закрытие сокета
/// \param[in] socket_id Номер сокета
/// \param[in] addr_name Название pipe файла, по которому общается сокет
///
void closeSoket(int socket_id, const sockaddr& addr){
    shutdown(socket_id, SHUT_RDWR);
    close(socket_id);
    unlink(addr.sa_data);
}

///
/// \brief Закрывает все сокеты
/// \param[in] list_of_sockets Список сокетов
///
void closeAll(const vector<int>& list_of_sockets, const vector<sockaddr>& list_of_sockets_addrs){
    for(auto i {0}; i < list_of_sockets.size(); i++){
        closeSoket(list_of_sockets[i], list_of_sockets_addrs[i]);
    }
}

void workWithSockets(){

    //  Время ожидания задач на дочернем процессе (в сек)
    int seconds_wait_tasks = 3;
    // Максимальный размер чтения данных из памяти
    int MAX_MSG_SIZE = 2048;

    int MAX_MATRIX_SIZE = floor(sqrt(static_cast<double>(MAX_MSG_SIZE - sizeof(int) * 2 - sizeof(long long) // long long int - под длину сообщения, один int под номер задачи, второй - под размер матрицы (см. конвертацию матрицы в char*)
                                                         ) / sizeof(double)));      //  Округление в меньшую сторону

    unsigned int    number_workers,
                    number_matrix,
                    matrix_size;
    double  max,
            min;

    //  Получение данных от пользователя
    cout << "Введите число работников: дочерних процессов: ";
    cin >> number_workers;
    cout << "Введите число матриц, которые вы хотите сгенерировать: ";
    cin >> number_matrix;
    cout << "Введите размерность матрицы (максимальный размер матрицы при максимальном размере передаваемого сообщения (" << MAX_MSG_SIZE << ") равен " << MAX_MATRIX_SIZE << "): ";
    cin >> matrix_size;
    if (matrix_size > MAX_MATRIX_SIZE){
        cout << "Введён размер матрицы, который превышает максимальный допустимый! Для увеличения - измените максимальный размер передаваемого сообщения!\n";
        return;
    }
    cout << "Введите диапазон значений, в пределах которых вы хотите генерировать значения матрицы: ";
    cin >> min >> max;
    cout << "-----info-----\n";
    cout    << "Печать введённых данных:\n"
            << "Число работников (дочерних процессов): " << number_workers << endl
            << "Число гененрируемых матриц: " << number_matrix << endl
            << "Интервал генерируемых значений матрицы: [" << min << "; " << max << "]\n";
    cout << "-----info-----\n";

    pid_t *workers_pid = new pid_t[number_workers];
    int *status = new int[number_workers];
    // Статус -1 - процесс не закончился, любое отличное значение - завершился (0 - удача, больше 0 - ошибка)
    memset(status, -1, sizeof(int) * number_workers);

    int socket_main = socket(AF_UNIX, SOCK_DGRAM, 0);    //  Сокет основного процесса
    if (socket_main < 0){
        cout << "Родитель: Ошибка создания сокета родительского процесса!\n";
        return;
    }

    string main_socket_name = "server";     //  Имя сокета основного процесса
    sockaddr main_socker_struct;            //  Структура, которая описывает сокет
    main_socker_struct.sa_family = AF_UNIX;
    strcpy(main_socker_struct.sa_data, main_socket_name.data());

    //  Привязали к сокету его данные
    if(bind(socket_main, &main_socker_struct, sizeof(main_socker_struct)) == -1){
        cout << "Родитель: Ошибка привязывания сокета к адресу!\n";
        printf("Error: %s\n", strerror(errno));
        closeSoket(socket_main, main_socker_struct);
        return;
    }

    //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
    const int flags_socket_main = fcntl(socket_main, F_GETFL, 0);
    fcntl(socket_main, F_SETFL, flags_socket_main | O_NONBLOCK);

    //  Создаём сокеты для работников
    vector<int> workers_sockets{};
    vector<sockaddr> workers_sockets_addr{};
    for(auto i {0}; i < number_workers; i++){
        workers_sockets.push_back(socket(AF_UNIX, SOCK_DGRAM, 0));
        if  (workers_sockets[i] < 0){
            cout << "Родитель: Ошибка создания сокета для дочернего процесса!\n";
            printf("Error: %s\n", strerror(errno));
            closeAll(workers_sockets, workers_sockets_addr);
            closeSoket(socket_main, main_socker_struct);
            return;
        }
        workers_sockets_addr.push_back(sockaddr{});
        string worker_socket_name = "worker" + to_string(i);   //  Имя сокета основного процесса
        workers_sockets_addr[i].sa_family = AF_UNIX;
        strcpy(workers_sockets_addr[i].sa_data, worker_socket_name.data());
        //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
        if(bind(workers_sockets[i], &workers_sockets_addr[i], sizeof(workers_sockets_addr[i].sa_family) + worker_socket_name.size()) == -1){
            cout << "Родитель: Ошибка привязывания сокета работника к адресу!\n";
            printf("Error: %s\n", strerror(errno));
            closeSoket(socket_main, main_socker_struct);
            closeAll(workers_sockets, workers_sockets_addr);
            return;
        }
        //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
        const int flags_socket_child = fcntl(workers_sockets[i], F_GETFL, 0);
        fcntl(workers_sockets[i], F_SETFL, flags_socket_child | O_NONBLOCK);
    }

    int from_main_with_workers[2];    //  Pipe из главного процесса с номерами работников, которые они забирают сами

    if (pipe(from_main_with_workers) == -1){  //  Создаем pipe
        closeSoket(socket_main, main_socker_struct);
        closeAll(workers_sockets, workers_sockets_addr);
        cout << "Родитель: Ошибка создания потока из родителя с номерами работников!\n";
        printf("Error: %s\n", strerror(errno));
        return;
    }

    //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
    const int flags_from_main = fcntl(from_main_with_workers[0], F_GETFL, 0);
    fcntl(from_main_with_workers[0], F_SETFL, flags_from_main | O_NONBLOCK);

    //  Выдаём номер работника в общий pipe
    for(int i {0}; i < number_workers; i++){
        write(from_main_with_workers[1], &i, sizeof(int));
    }

    char    *matrix_buf,    //  Буфер для матриц
            *message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
    int data_size;          //  Размер матрицы в формате char*
    long long message_size; //  Размер передаваемого сообщения

    //  Создаём дочерние процессы
    for(unsigned int i {0}; i < number_workers; i++){
        switch (workers_pid[i] = fork()) {
        case -1:{
            cout << "Ошибка создания worker-а!\n";
            exit(1);
        }break;
        case 0:{//Это дочерний процесс
            ///
            /// \brief Функциональный объект для печати
            ///
            class PrintChildInfo {
            public:
                ///
                /// \brief Печатает информацию о процессе
                /// \param[in] str Печатоемое сообщение
                ///
                void operator ()(const string& str){
                    cout    << "->Это дочерний процесс (" << getpid() << "), мой родитель (" << getppid() << "):\n"
                            << "-->" << str << endl;
                }
            };
            PrintChildInfo printChildInfo;  //  Функциональный объект для печати инфы о дочернем процессе (сделано так, т.к. если сделать её просто функцией в глобальной области видимости, то pid родителя будет равен 1!)
            int worker_number,
                number_try_to_read {0},     //  Число неудачных попыток чтения
                max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения
            unsigned int local_data_size;          //  Размер матрицы в формате char*
            long long local_message_size; //  Размер передаваемого сообщения

            char    *local_matrix_buf,           //  Буфер для матриц
                    *local_message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
            bool success {false};   //  Флаг удачи

            printChildInfo("Считываю свой номер работника...");
            while(number_try_to_read != max_number_of_read_fails){  //  Считывваем свой номер работника, чтобы определить поток, из которого будем потом читать
                if(read(from_main_with_workers[0], &worker_number, sizeof(int)) <= 0){
                    number_try_to_read++;
                    printChildInfo("Не удалось определить свой номер! Осталось попыток на чтение (" + to_string(max_number_of_read_fails - number_try_to_read) + ")! Пытаюсь снова считать свой номер работника...");
                } else {
                    success = true;
                    break;
                }
                sleep(1);
            }
            if (!success){
                printChildInfo("Не смог получить свой рабочий номер! Завершаю свою работу!");
                exit(1);    // Не смогли получить свой номер
            }
            printChildInfo("Мой рабочий номер: " + to_string(worker_number));

            printChildInfo("Ожидаю задачи (" + to_string(seconds_wait_tasks) + " секунд(ы) буду ждать задачи, затем завершу свою работу)!");

            local_message_buf = new char[MAX_MSG_SIZE];
            local_matrix_buf = new char[MAX_MSG_SIZE];
            memset(local_matrix_buf, 0, MAX_MSG_SIZE);
            memset(local_message_buf, 0, MAX_MSG_SIZE);
            number_try_to_read = 0;

            while(number_try_to_read != max_number_of_read_fails){
                if(recvfrom(workers_sockets[worker_number], local_message_buf, MAX_MSG_SIZE, 0, 0, 0) > 0){ //  Тут считываем MAX_MSG_SIZE, т.к. при чтении части сообщения остальная удаляется
                    memcpy(&local_message_size, local_message_buf, sizeof(long long));
                    if(local_message_size){
                        number_try_to_read = 0;
                        printChildInfo("Получили пакет из " + to_string(local_message_size)+ " байт!");
                        unsigned int task_number;
                        extractMessageFromSending(local_message_buf, &task_number, local_matrix_buf);
                        memset(local_message_buf, 0, local_message_size);
                        square_matrix tmp_matrix = fromByteToMatrix(local_matrix_buf);
                        memset(local_matrix_buf, 0, local_message_size - sizeof(long long) - sizeof(int));
                        printChildInfo("Получили задачу номер " + to_string(task_number) + ":");
                        printMatrix(tmp_matrix);
                        tmp_matrix = findReverseMatrix(tmp_matrix);
                        printChildInfo("Решили задачу:");
                        printMatrix(tmp_matrix);
                        fromMatrixToByte(tmp_matrix, local_matrix_buf, &local_data_size);
                        compressMessageForSending(task_number, local_matrix_buf, local_data_size, local_message_buf, &local_message_size);
                        memset(local_matrix_buf, 0, local_data_size);
                        int counter = 0;
                        socklen_t tmp_size = sizeof(main_socker_struct);
                        while (counter != max_number_of_read_fails){
                            if (sendto(socket_main, local_message_buf, local_message_size, 0, &main_socker_struct, tmp_size) > 0){
                                printChildInfo("Удачно отправили родителю ответ на задачу!");
                                break;
                            } else{
                                counter++;
                                printChildInfo("Ошибка отправки ответа родителю: " + string(strerror(errno)) + ". Число попыток для отправки <" + to_string(max_number_of_read_fails- counter) + ">...");
                                sleep(seconds_wait_tasks);
                            }
                        }
                    }
                    memset(local_message_buf, 0, local_message_size);
                } else{
                    number_try_to_read++;
                    printChildInfo("Задачи от сервера не получены! Число попыток получить задачи <" + to_string(max_number_of_read_fails- number_try_to_read) + ">! Ожидание задач от сервера...");
                    sleep(1);
                }
            }
            printChildInfo("Завершаю свою работу!");
            delete[] local_matrix_buf;
            delete[] local_message_buf;
            closeSoket(workers_sockets[worker_number], workers_sockets_addr[worker_number]);
            exit(0);
        }break;
        default://Это родительский процесс
            break;
        }
    }

    map<unsigned int, square_matrix>    tasks,          //  Сгенерированные задачи
                                        tasks_result;   //  Полученные ответы на задачи

    //  Создаём задачи
    for(auto i {0}; i < number_matrix; i++){
        tasks[i] = genMatrix(matrix_size, min, max);
        cout << "Задача номер " << i << ":\n";
        printMatrix(tasks[i]);
    }

    int number_try_to_read {0},     //  Число неудачных попыток чтения
        max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения


    //  Отправляем задачи работникам
    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);

    for(auto i {0}; i < number_matrix; i++){
        number_try_to_read = 0;
        fromMatrixToByte(tasks[i], matrix_buf, &matrix_size);
        compressMessageForSending(i, matrix_buf, matrix_size, message_buf, &message_size);
        memset(matrix_buf, 0, matrix_size);
        while (number_try_to_read != max_number_of_read_fails){
            socklen_t tmp_len = sizeof(workers_sockets_addr[i % number_workers]);
            if (sendto(workers_sockets[i % number_workers], message_buf, message_size, 0, &workers_sockets_addr[i % number_workers], tmp_len) < 0){
                cout << "Родитель: Ошибка отправки задачи номер <" << i << "> работнику номер <" << i % number_workers << ">!\n";
                printf("Error: %s\n", strerror(errno));
                number_try_to_read++;
                sleep(1);
            } else {
                cout << "Родитель: Отправил задачу <" << i << "> рабочему номер " << i % number_workers << endl;
                break;
            }
        }
        memset(message_buf, 0, message_size);
    }

    delete[] message_buf;
    delete[] matrix_buf;
    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);
    //  Теперь получаем ответы
    number_try_to_read = 0;
    while (number_try_to_read != max_number_of_read_fails){
        if (recvfrom(socket_main, message_buf, MAX_MSG_SIZE, 0, 0, 0) > 0){
            memcpy(&message_size, message_buf, sizeof(long long));
            cout << "Родитель: получили сообщение длиной " << message_size << "!\n";
            unsigned int task_number;
            extractMessageFromSending(message_buf, &task_number, matrix_buf);
            memset(message_buf, 0, message_size);
            tasks_result[task_number] = fromByteToMatrix(matrix_buf);
            memset(matrix_buf, 0, message_size - sizeof(long long) - sizeof(int));
            cout << "Родитель: получили ответ на задачу номер " << task_number << ":\n";
            printMatrix(tasks_result[task_number]);
        } else{
            cout << "Родитель: не получил ответы на задачи! Число попыток для проверки наличия ответ задач <" << max_number_of_read_fails - number_try_to_read << ">...\n";
            number_try_to_read++;
            sleep(seconds_wait_tasks);
        }
    }


    //  Тут ждём завершение процессов-worker-ов
    for(int i {0}; i < number_workers; i++){
        waitpid(workers_pid[i], &status[i], 0);
        switch (status[i]) {
        case 0:
            cout << "Родитель: Процесс-worker (" << workers_pid[i] << ") завершился удачно\n";
            break;
        default:
            cout << "Родитель: Процесс-worker (" << workers_pid[i] << ") завершился с ошибкой! Код ошибки: < " << status[i] << "> (" << processErrorToString(static_cast<ProcessError>(status[i])) << ")\n";
            break;
        }
    }
    cout << "Родитель: Дочерние процессы выключились!\n";

    delete[] workers_pid;
    delete[] status;
    delete[] matrix_buf;
    delete[] message_buf;

    //  Печать решения задач
    for(auto i {0}; i < number_matrix; i++){
        cout << "Родитель: Задача номер <" << i << ">:\n";
        printMatrix(tasks[i]);
        cout << "Родитель: Её решение:\n";
        if (tasks_result.count(i)){
            printMatrix(tasks_result[i]);
        } else {
            cout << "Родитель: Нет решения!\n";
        }
    }
    closeSoket(socket_main, main_socker_struct);
    closeAll(workers_sockets, workers_sockets_addr);
    close(from_main_with_workers[0]);
    close(from_main_with_workers[1]);
}

//  В разработке
void workTiwhSharedMemory(){

    //  Время ожидания задач на дочернем процессе (в сек)
    int seconds_wait_tasks = 3;
    // Максимальный размер чтения данных из памяти
    int MAX_MSG_SIZE = 2048;

    int MAX_MATRIX_SIZE = floor(sqrt(static_cast<double>(MAX_MSG_SIZE - sizeof(int) * 2 - sizeof(long long) - sizeof(char) // long long int - под длину сообщения, один int под номер задачи, второй - под размер матрицы (см. конвертацию матрицы в char*), char - для флага готовности задачи
                                                         ) / sizeof(double)));      //  Округление в меньшую сторону

    unsigned int    number_workers,
                    number_matrix,
                    matrix_size;
    double  max,
            min;

    //  Получение данных от пользователя
    cout << "Введите число работников: дочерних процессов: ";
    cin >> number_workers;
    cout << "Введите число матриц, которые вы хотите сгенерировать: ";
    cin >> number_matrix;
    cout << "Введите размерность матрицы (максимальный размер матрицы при максимальном размере передаваемого сообщения (" << MAX_MSG_SIZE << ") равен " << MAX_MATRIX_SIZE << "): ";
    cin >> matrix_size;
    if (matrix_size > MAX_MATRIX_SIZE){
        cout << "Введён размер матрицы, который превышает максимальный допустимый! Для увеличения - измените максимальный размер передаваемого сообщения!\n";
        return;
    }
    cout << "Введите диапазон значений, в пределах которых вы хотите генерировать значения матрицы: ";
    cin >> min >> max;
    cout << "-----info-----\n";
    cout    << "Печать введённых данных:\n"
            << "Число работников (дочерних процессов): " << number_workers << endl
            << "Число гененрируемых матриц: " << number_matrix << endl
            << "Интервал генерируемых значений матрицы: [" << min << "; " << max << "]\n";
    cout << "-----info-----\n";

    string shared_memory_block_main_name = "server";
    //  Создаём разделяемую память для основного процесса
    int shared_memory_block_main = shm_open(shared_memory_block_main_name.data(), O_CREAT | O_RDWR, 777);
    if (shared_memory_block_main < 0){
        cout << "Родитель: Ошибка создания разделяемого блока памяти родительского процесса :" << strerror(errno) << endl;
        return;
    }

    //  Задаём размер этой памяти
    if (ftruncate(shared_memory_block_main, MAX_MSG_SIZE * number_matrix) < 0){
        cout << "Родитель: ошибка расширения разделяемой памяти родительского процесса: " << strerror(errno) << endl;
        shm_unlink(shared_memory_block_main_name.data());
        return;
    }

    //  Формируем указатель с определёнными флагами для доступа к разделяемой памяти
    void *shared_memory_block_main_header = mmap(NULL, MAX_MSG_SIZE * number_matrix, PROT_READ | PROT_WRITE, MAP_SHARED, shared_memory_block_main, 0);

    if (shared_memory_block_main_header == MAP_FAILED){
        cout << "Родитель: Ошибка получения доступа к разделяемой памяти родителя: " << strerror(errno) << endl;
        shm_unlink(shared_memory_block_main_name.data());
        return;
    }

    memset(shared_memory_block_main_header, 0, MAX_MSG_SIZE * number_matrix);

    pid_t *workers_pid = new pid_t[number_workers];
    int *status = new int[number_workers];
    // Статус -1 - процесс не закончился, любое отличное значение - завершился (0 - удача, больше 0 - ошибка)
    memset(status, -1, sizeof(int) * number_workers);

    int from_main_with_workers[2];    //  Pipe из главного процесса с номерами работников, которые они забирают сами

    if (pipe(from_main_with_workers) == -1){  //  Создаем pipe
        cout << "Родитель: Ошибка создания потока из родителя с номерами работников:" << strerror(errno) << endl;
        shm_unlink(shared_memory_block_main_name.data());
        return;
    }

    //  Делаем дескрипторы неблокируемыми, чтобы не повиснуть на read запросах
    const int flags_from_main = fcntl(from_main_with_workers[0], F_GETFL, 0);
    fcntl(from_main_with_workers[0], F_SETFL, flags_from_main | O_NONBLOCK);

    //  Выдаём номер работника в общий pipe
    for(int i {0}; i < number_workers; i++){
        write(from_main_with_workers[1], &i, sizeof(int));
    }

    char    *matrix_buf,    //  Буфер для матриц
            *message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
    unsigned int data_size;          //  Размер матрицы в формате char*
    long long message_size; //  Размер передаваемого сообщения
    char flag = static_cast<char>(false);

    //  Создаём дочерние процессы
    for(unsigned int i {0}; i < number_workers; i++){
        switch (workers_pid[i] = fork()) {
        case -1:{
            cout << "Ошибка создания worker-а!\n";
            exit(1);
        }break;
        case 0:{//Это дочерний процесс
            ///
            /// \brief Функциональный объект для печати
            ///
            class PrintChildInfo {
            public:
                ///
                /// \brief Печатает информацию о процессе
                /// \param[in] str Печатоемое сообщение
                ///
                void operator ()(const string& str){
                    cout    << "->Это дочерний процесс (" << getpid() << "), мой родитель (" << getppid() << "):\n"
                            << "-->" << str << endl;
                }
            };
            PrintChildInfo printChildInfo;  //  Функциональный объект для печати инфы о дочернем процессе (сделано так, т.к. если сделать её просто функцией в глобальной области видимости, то pid родителя будет равен 1!)
            int worker_number,
                number_try_to_read {0},     //  Число неудачных попыток чтения
                max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения
            unsigned int local_data_size;          //  Размер матрицы в формате char*
            long long local_message_size; //  Размер передаваемого сообщения

            char    *local_matrix_buf,           //  Буфер для матриц
                    *local_message_buf;   //  Буфер для сообщений, передаваемых в pipe-ах
            local_message_buf = new char[MAX_MSG_SIZE];
            local_matrix_buf = new char[MAX_MSG_SIZE];
            bool success {false};   //  Флаг удачи
            printChildInfo("Считываю свой номер работника...");
            while(number_try_to_read != max_number_of_read_fails){  //  Считывваем свой номер работника, чтобы определить поток, из которого будем потом читать
                if(read(from_main_with_workers[0], &worker_number, sizeof(int)) <= 0){
                    number_try_to_read++;
                    printChildInfo("Не удалось определить свой номер! Осталось попыток на чтение (" + to_string(max_number_of_read_fails - number_try_to_read) + ")! Пытаюсь снова считать свой номер работника...");
                } else {
                    success = true;
                    break;
                }
                sleep(1);
            }
            if (!success){
                printChildInfo("Не смог получить свой рабочий номер! Завершаю свою работу!");
                exit(1);    // Не смогли получить свой номер
            }
            printChildInfo("Мой рабочий номер: " + to_string(worker_number));
            printChildInfo("Ожидаю задачи (" + to_string(seconds_wait_tasks) + " секунд(ы) буду ждать задачи, затем завершу свою работу)!");
            memset(local_matrix_buf, 0, MAX_MSG_SIZE);
            memset(local_message_buf, 0, MAX_MSG_SIZE);
            number_try_to_read = 0;
            int counter,
                number_tasks = number_matrix / number_workers;
            if (number_matrix % number_workers && worker_number < number_matrix % number_workers){
                number_tasks++;
            }
            printChildInfo("Я могу решать до " + to_string(number_tasks) + " задач!");
            while(number_try_to_read != max_number_of_read_fails){
                counter = 0;
                for(auto i {0}; i < number_tasks; i++){
                    int current_task_number = worker_number + number_workers * i;
                    memcpy(&flag, shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number, sizeof(char));
                    printChildInfo("Задача номер " + to_string(i) + " флаг: " + to_string(static_cast<unsigned int>(flag)));
                    if (flag == 0){
                        memcpy(&local_message_size, shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number + sizeof(char), sizeof(local_message_size));
                        if (local_message_size != 0){
                            printChildInfo("Получили сообщение длинной " + to_string(local_message_size) + " байт!");
                            memcpy(local_message_buf, shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number + sizeof(char), local_message_size);
                            unsigned int task_number;
                            extractMessageFromSending(local_message_buf, &task_number, local_matrix_buf);
                            memset(local_message_buf, 0, local_message_size);
                            square_matrix tmp_matrix = fromByteToMatrix(local_matrix_buf);
                            memset(local_matrix_buf, 0, local_message_size - sizeof(long long) - sizeof(int));
                            memset(shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number, 0 , local_message_size + sizeof(char));
                            printChildInfo("Получили задачу номер " + to_string(task_number) + ":");
                            printMatrix(tmp_matrix);
                            tmp_matrix = findReverseMatrix(tmp_matrix);
                            printChildInfo("Нашли решение задачи:");
                            printMatrix(tmp_matrix);
                            fromMatrixToByte(tmp_matrix, local_matrix_buf, &local_data_size);
                            compressMessageForSending(task_number, local_matrix_buf, local_data_size, local_message_buf, &local_message_size);
                            memset(local_matrix_buf, 0, local_data_size);
                            flag = 1;
                            memcpy(shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number, &flag, sizeof(flag));
                            memcpy(shared_memory_block_main_header + MAX_MSG_SIZE * current_task_number + sizeof(char), local_message_buf, local_message_size);
                            memset(local_message_buf, 0, local_message_size);
                        } else {
                            counter++;
                        }
                    } else {
                        counter++;
                    }
                }
                if (counter == number_tasks){
                    number_try_to_read++;
                    printChildInfo("Не получали новых задач! Число попыток проверки наличия новых задач <" + to_string(max_number_of_read_fails - number_try_to_read) + ">...");
                    sleep(seconds_wait_tasks);
                } else {
                    number_try_to_read = 0;
                }
            }
            printChildInfo("Завершаю свою работу!");
            delete[] local_matrix_buf;
            delete[] local_message_buf;
            exit(0);
        }break;
        default://Это родительский процесс
            break;
        }
    }

    map<unsigned int, square_matrix>    tasks,          //  Сгенерированные задачи
                                        tasks_result;   //  Полученные ответы на задачи

    //  Создаём задачи
    for(auto i {0}; i < number_matrix; i++){
        tasks[i] = genMatrix(matrix_size, min, max);
        cout << "Задача номер " << i << ":\n";
        printMatrix(tasks[i]);
    }

    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);
    //  Упаковываем задачи в разделяемую память
    //  Формат задач: (флаг готовности[char])(длина данных[long long])(номер задачи[int])(данные матрицы(char*))
    for(auto i {0}; i < number_matrix; i++){
        memcpy(shared_memory_block_main_header + MAX_MSG_SIZE * i, &flag, sizeof(flag));
        fromMatrixToByte(tasks[i], matrix_buf, &data_size);
        compressMessageForSending(i, matrix_buf, data_size, message_buf, &message_size);
        memset(matrix_buf, 0, data_size);
        memcpy(shared_memory_block_main_header + MAX_MSG_SIZE * i + sizeof(char), message_buf, message_size);
        memset(message_buf, 0, message_size);
    }

    delete[] message_buf;
    delete[] matrix_buf;
    message_buf = new char[MAX_MSG_SIZE];
    matrix_buf = new char[MAX_MSG_SIZE];
    memset(message_buf, 0, MAX_MSG_SIZE);
    memset(matrix_buf, 0, MAX_MSG_SIZE);
    int     number_try_to_read {0},     //  Число неудачных попыток чтения
            max_number_of_read_fails {3};   //  Допустимое число неудачных попыток чтения
    //  Ждём ответы
    while(number_try_to_read != max_number_of_read_fails){
        int counter {0};
        for (auto i {0}; i < number_matrix; i++){
            memcpy(&flag, shared_memory_block_main_header + MAX_MSG_SIZE * i, sizeof(char));
            if (flag == 1){
                memcpy(&message_size, shared_memory_block_main_header + MAX_MSG_SIZE * i + sizeof(char), sizeof(long long));
                memcpy(message_buf, shared_memory_block_main_header + MAX_MSG_SIZE * i + sizeof(char), message_size);
                unsigned int task_number;
                extractMessageFromSending(message_buf, &task_number, matrix_buf);
                memset(message_buf, 0, message_size);
                tasks_result[task_number] = fromByteToMatrix(matrix_buf);
                memset(matrix_buf, 0, message_size - sizeof(long long) - sizeof(int));
                cout << "Родитель: Получили решение задачи номер " << task_number << ":\n";
                printMatrix(tasks_result[task_number]);
                flag = 2;
                memset(shared_memory_block_main_header + MAX_MSG_SIZE * i, 0, message_size + sizeof(char));
                memcpy(shared_memory_block_main_header + MAX_MSG_SIZE * i, &flag, sizeof(flag));
            } else {
                counter++;
            }
        }
        if (counter == number_matrix){
            number_try_to_read++;
            cout << "Родитель: Не получено новых задач! Число попыток для проверки новых задач <" << max_number_of_read_fails - number_try_to_read << ">...\n";
            sleep(seconds_wait_tasks);
        } else {
            number_try_to_read = 0;
        }
    }

    //  Тут ждём завершение процессов-worker-ов
    for(int i {0}; i < number_workers; i++){
        waitpid(workers_pid[i], &status[i], 0);
        switch (status[i]) {
        case 0:
            cout << "Родитель: Процесс-worker (" << workers_pid[i] << ") завершился удачно\n";
            break;
        default:
            cout << "Родитель: Процесс-worker (" << workers_pid[i] << ") завершился с ошибкой! Код ошибки: < " << status[i] << "> (" << processErrorToString(static_cast<ProcessError>(status[i])) << ")\n";
            break;
        }
    }
    cout << "Родитель: Дочерние процессы выключились!\n";
    delete[] workers_pid;
    delete[] status;
    delete[] matrix_buf;
    delete[] message_buf;

    //  Печать решения задач
    for(auto i {0}; i < number_matrix; i++){
        cout << "Родитель: Задача номер <" << i << ">:\n";
        printMatrix(tasks[i]);
        cout << "Родитель: Её решение:\n";
        if (tasks_result.count(i)){
            printMatrix(tasks_result[i]);
        } else {
            cout << "Родитель: Нет решения!\n";
        }
    }

    shm_unlink(shared_memory_block_main_name.data());
    close(from_main_with_workers[0]);
    close(from_main_with_workers[1]);
}

int main(){

//    workWithPipes();
//    workWithSockets();
    workTiwhSharedMemory();

    return 0;
}
