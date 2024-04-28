#include "gauss.h"

void printMatrix(const square_matrix& matrix){
    if (!matrix.empty()){
        for(const vector<double>& row: matrix){
            if (!row.empty()){
                cout << '[';
                for(const double row_elem: row){
                    cout << row_elem << ' ';
                }
                cout << "]\n";
            }
        }
    }
    cout << endl;
}

bool checkMatrix(const square_matrix& matrix){
    auto number_rows = matrix.size();
    for (const vector<double>& row: matrix){
        if (row.size() != number_rows){
            return false;
        }
    }
    return true;
}

void setFlag(bool *flag, bool flag_resume){
    if (flag != nullptr){
        *flag = flag_resume;
    }
}

square_matrix createInverseMatrixTemplate(const unsigned int size){
    square_matrix matrix_template{};
    unsigned int j {0}; //Счётчик
    matrix_template.resize(0);
    for(auto i {0}; i < size; i++){
        matrix_template.push_back(vector<double>{});
        for(j = 0; j < size; j++){
            matrix_template[i].push_back(0);
        }
        matrix_template[i][i] = 1;
    }
    return matrix_template;
}

double summ(const double first, const double second){
    return first + second;
}

double sub(const double first, const double second){
    return first - second;
}

double div(const double first, const double second){
    if(second != 0){
        return first / second;
    } else {
        return 0;
    }
}

double mul(const double first, const double second){
    return first * second;
}

vector<double> rowOperation(vector<double>& row, double value, double(*operation)(double, double)){
    vector<double> result{};
    result.resize(0);
    if (!row.empty()){
        for (double elem: row){
            result.push_back(operation(elem, value));
        }
    }
    return result;
}

vector<double> rowsOperation(const vector<double>& first, const vector<double>& second, double(*operation)(double, double)){
    vector<double> result{};
    result.resize(0);
    if (first.size() == second.size()){
        for(auto i {0}; i < first.size(); i++){
            result.push_back(operation(first[i], second[i]));
        }
    }
    return result;
}

square_matrix findReverseMatrix(const square_matrix& matrix, bool *success){
    square_matrix result{};
    //Проверка на корректность матрицы для дальнейшей работы над ней
    if (matrix.empty() || !checkMatrix(matrix)){
        setFlag(success, false);
        result.resize(0);
        return result;
    }

    square_matrix matrix_copy = matrix;
    result = createInverseMatrixTemplate(matrix.size());

    for(auto i {0}; i < matrix_copy.size(); i++){
        //  Если диагональный элемент равен нулю, то ищем строку ниже данной, в которой такой элемент не будет равен нулю
        if (matrix_copy[i][i] == 0){
            //  Флаг замены строк
            bool flag_swap {false};
            for(auto j {i}; j < matrix_copy.size(); j++){
                //  Если нашли такую строку, то меняем их местами
                if(matrix_copy[j][i] != 0){
                    flag_swap = true;
                    matrix_copy[i].swap(matrix_copy[j]);
                    result[i].swap(result[j]);
                    break;
                }
            }
            //  Если замены строк не было, то нет смысла работать с данным диагональным элементом, поэтому идём на следующую итерацию
            if (!flag_swap){
                continue;
            }
        }
        //Выполняем операцию над финальной матрицей, т.к. они зеркальны
        result[i] = rowOperation(result[i],         //  текущая строка итоговой матрицы
                                 matrix_copy[i][i], //  диагональный элемент матрицы, в которой работаем
                                 div);              // деление
        //Делим строку на диагональный элемент
        matrix_copy[i] = rowOperation(matrix_copy[i],   //  текущая строка
                     matrix_copy[i][i],                 //  текущий диагональный элемент
                     div);                              //  деление
        //Идём по строкам, ниже данной и выполняем обнуление столбца, в котором находится диагональный элемент.
        //Обнуляются только те элементы матрицы, которые ниже диагонального элемента
        for (auto j {0}; j < matrix_copy.size(); j++){
            //Проверка на то, что не будем вычитать сами себя
            if(j != i){
                //  Выполняем те же операции, что и с обычной матрицей, но только со строкаи из финальной матрицы, но значениями из текущей
                //  Более подробное описание см.ниже
                result[j] = rowsOperation(result[j],
                                          rowOperation(result[i],
                                                       div(-matrix_copy[j][i], matrix_copy[i][i]),
                                                       mul),
                                          summ);
                //  Складываем две строки так, чтобы элемент в столбце, что и диагональный в другой строке стал нулём
                matrix_copy[j] = rowsOperation(matrix_copy[j],                                      //  строка, в которой обнуляем столбец
                                                                                                    //   умножаем складываемую строку с диагональным эелементов так, чтобы при сложении двух строк элемент в том же столбце, что и диагональный в строке стал равен 0
                                                                                                    //   результатом будет перемноженная строка с диагональным элементом
                                               rowOperation(matrix_copy[i],                             //  текущая строка с диагональным элементом
                                                            div(-matrix_copy[j][i], matrix_copy[i][i]), //  расчёт коэф-та, на котороый надо будет умножить строку, чтобы при сложении двух элементов (диагональный и тот, с которым складываем) вышел 0
                                                            mul),                                       //  умножение
                                               summ);                                               // суммирование
            }
        }
    }

    setFlag(success, true);
    return result;
}

square_matrix genMatrix(unsigned int size, double min, double max, bool is_double)
{
    if(min > max){
        double tmp = max;
        max = min;
        min = tmp;
    }
    vector<vector<double>> result{};
    result.resize(0);
    for(auto i {0}; i < size; i++){
        result.push_back(vector<double>{});
        for (auto j {0}; j < size; j++){
            if (is_double){
                result[i].push_back((double)(rand())/RAND_MAX*(max - min) + min);
            } else {
                result[i].push_back(min + rand() % (static_cast<int>(max) - static_cast<int>(min)));
            }
        }
    }
    return result;
}

void fromMatrixToByte(const square_matrix &matrix, char *&buf, unsigned int *size)
{
    if (!matrix.empty()){
        buf = new char[matrix.size() * matrix.size() * sizeof(double) + sizeof(int)];
        memset(buf, 0, matrix.size() * matrix.size() * sizeof(double) + sizeof(int));
        int size = matrix.size();
        memcpy(buf, &size, sizeof(int));
        int counter = 0;
        for(const vector<double>& row: matrix){
            for(const double elem: row){
                memcpy(buf + counter * sizeof(double) + sizeof(int), &elem, sizeof(double));
                counter++;
            }
        }
    }
    *size = sizeof(double) * matrix.size() * matrix.size() + sizeof(int);
}

square_matrix fromByteToMatrix(char *buf)
{
    square_matrix result{};
    result.resize(0);
    int size;
    memcpy(&size, buf, sizeof(int));
    int counter {0};
    if(size != 0){
        for(int i = 0; i < size; i++){
            result.push_back(vector<double>{});
            for(int j = 0 ; j < size; j++){
                result[i].push_back(0);
                memcpy(&result[i][j], buf + counter * sizeof(double) + sizeof(int), sizeof(double));
                counter++;
            }
        }
    }
    return result;
}
