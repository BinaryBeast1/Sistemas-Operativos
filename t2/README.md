# cmatch - Motor de Emparejamiento Concurrente 

Este programa es una simulación de un torneo de Tic-Tac-Toe (Gato) concurrente. Múltiples hilos (threads) actúan como jugadores que buscan oponentes simultáneamente basándose en su nivel de ELO y tiempo de espera, compitiendo por un número limitado de tableros disponibles.

# Requisitos y Compilación

El sistema está desarrollado estrictamente en C17 utilizando POSIX Threads (`pthread`). Para compilar el programa, abra su terminal en el directorio del proyecto y ejecute:

# gcc -Wall -Wextra -std=c17 -pthread cmatch.c -o cmatch -lm