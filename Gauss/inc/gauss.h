#include <iostream>
#include <vector>
#include "time.h"
#include "stdlib.h"
#include <cstring>

using namespace std;

///
/// \brief Квадратная матрица
///
using square_matrix = vector<vector<double>>;

///
/// \brief Печать матрицы
/// \param[in] matrix Сама матрица
///
void printMatrix(const square_matrix& matrix);

///
/// \brief Проверка того, что матрица квадратная
/// \param[in] matrix Сама матрица
/// \return Результат проверки
/// \note
/// Результат проверки:
/// * true - матрица квадратная
/// * false - нет
///
bool checkMatrix(const square_matrix& matrix);

///
/// \brief Установка флага
/// \param[in, out] flag Флаг
/// \param[in] flag_resume Режим флага
///
void setFlag(bool *flag, bool flag_resume);

///
/// \brief Создаёт шаблон обратной матрицы, где по диагонали стоят единицы
/// \param[in] size Размер квадратной матрицы
/// \return Сама матрица
///
square_matrix createInverseMatrixTemplate(const unsigned int size);

///
/// \brief Сумма двух чисел с плавающей запятой
/// \param[in] first Первое число
/// \param[in] second Второе число
/// \return Результат сложения
///
double summ(const double first, const double second);

///
/// \brief Разность двух чисел с плавающей запятой
/// \param[in] first Первое число
/// \param[in] second Второе число
/// \return Результат вычитания
///
double sub(const double first, const double second);

///
/// \brief Деление двух чисел с плавающей запятой
/// \param[in] first Первое число
/// \param[in] second Второе число
/// \return Результат деления
///
double div(const double first, const double second);

///
/// \brief Умножение двух чисел с плавающей запятой
/// \param[in] first Первое число
/// \param[in] second Второе число
/// \return Результат умножения
///
double mul(const double first, const double second);

///
/// \brief Выполнение операции над строкой и числом
/// \param[in] row Строка
/// \param[in] value Число
/// \return Результат выполнения операции
///
vector<double> rowOperation(vector<double>& row, double value, double(*operation)(double, double));

///
/// \brief Выполнение операции между строками
/// \param[in] first Первая строка
/// \param[in] second Вторая строка
/// \param[in] operation Тип выполняемой операции (функция)
/// \return Результат выполнения операции
/// \note Возвращает пустой вектор в случае неудачи
///
vector<double> rowsOperation(const vector<double>& first, const vector<double>& second, double(*operation)(double, double));

///
/// \brief Поиск обратной матрицы методом Гаусса
/// \param[in] matrix Сама матрица
/// \param[out] success Флаг удачности выполнения операции
/// \return Обратная матрица к данной
/// \note
/// Флаг удачности:
/// * true - операция выполнена удачно
/// * false - неудача
/// \warning
/// В случае неудачи функция возвращает пустую матрицу
///
square_matrix findReverseMatrix(const square_matrix& matrix, bool *success = nullptr);

///
/// \brief Генерирует квадратную матрицу
/// \param[in] size Размер квадратной матрицы
/// \param[in] min Минимальное число
/// \param[in] max Максимальное число
/// \param[in] is_double Флаг того, будут числа целыми или же с плавающей запятой
/// \return Квадрантная матрица со случайными числами
///
square_matrix genMatrix(unsigned int size, double min, double max, bool is_double = false);

///
/// \brief Преобразование матрицы в набор байт
/// \param[in] matrix Матрица
/// \param[out] buf Буффер, куда записывается матрица
/// \param[out] size Размер созданного буфера
/// \warning Не забудь очистить память, выделенную под буфер!
///
void fromMatrixToByte(const square_matrix& matrix, char *&buf, unsigned int *size);

///
/// \brief Преобразование набора байт в матрицу
/// \param[in] buf Сам набор байт
/// \return Матрица
///
square_matrix fromByteToMatrix(char *buf);
