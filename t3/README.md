# memsim - Simulador de Políticas de Asignación de Memoria

Este programa implementa un simulador por consola que modela el gestor de memoria de un sistema operativo, usando partición dinámica con las políticas **First-Fit**, **Best-Fit** y **Worst-Fit**.

## Compilación

Para compilar el programa utilizando los estándares requeridos (C17) y banderas de advertencia estrictas, ejecuta en la consola:

```bash
gcc -Wall -Wextra -std=c17 memsim.c -o memsim
```

## Ejecución

El simulador se ejecuta por línea de comandos pasando tres argumentos:
1. `size`: Tamaño de la memoria simulada en bytes.
2. `policy`: Política de asignación (`FIRST_FIT`, `BEST_FIT` o `WORST_FIT`).
3. `trace_file`: Archivo de texto con las secuencias de instrucciones (`ALLOC`, `FREE`, `COMPACT`).

**Ejemplo de uso:**

```bash
./memsim 1024 FIRST_FIT traza1.txt
```

```bash
./memsim 1000 BEST_FIT traza_compact.txt
./memsim 1000 WORST_FIT traza_policies.txt
```

## Formato de los archivos de traza

Los archivos de traza `.txt` deben contener comandos en cada línea de la siguiente manera:
- `ALLOC <ID_Proceso> <TAMAÑO>`: Asigna `TAMAÑO` bytes al proceso `ID_Proceso`.
- `FREE <ID_Proceso>`: Libera la memoria asignada al proceso.
- `COMPACT`: Desplaza todos los bloques ocupados hacia el inicio y fusiona todo el espacio libre al final.
