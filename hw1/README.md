## Условие
Написать программу, которая пишет числа построчно.   
Программа может запускать несколько копий одновременно.   
Написать наиболее эффективно. Учесть все race-conditions.  

## Решение
Компиляция ```gcc main.c```  

Скрипт ```run.sh``` запускает параллельно 50 программ.  

Программа читает построчно файл ```file.txt```,  
к последнему прочитанному числу прибавляет единицу  
и записывает в конец.  

Доказательство правльности работы программы - последнее число в файле = 50.
