#include <inttypes.h>
/*** priority, nice, ready_threads are int,
recent_cpu, load_avg are real number 
decimal arithmetic is needed to calculate recent_cpu and load_avg
implement by using 17.14 fixed-point number representation ***/

#define F (1<<14) /*** fixed point 1 ***/
#define INT_MAX ((1<<31)-1)
#define INT_MIN (-(1<<31))
/*** x and y denote fixed_point numbers in 17.14 format
n is an integer ***/

int int_to_fp(int n);           /*** integer to fixed point ***/
int fp_to_int_round(int x);     /*** round FP to int ***/
int fp_to_int(int x);           /*** round down FP to int ***/
int add_fp(int x, int y);       /*** addition of FP ***/
int add_mixed(int x, int n);    /*** addition of FP and int ***/ 
int sub_fp(int x, int y);       /*** substraction of FP (x-y) ***/
int sub_mixed(int x, int n);    /*** substraction of FP and int (x-n) ***/
int mult_fp(int x, int y);      /*** multiplication of FP ***/
int mult_mixed(int x, int n);   /*** multiplication of FP and int ***/
int div_fp(int x, int y);       /*** division of FP (x/y) ***/
int div_mixed(int x, int n);    /*** division of FP and int (x/n) ***/

int int_to_fp(int n)            {return n*F;}
/*** round to nearest ***/
int fp_to_int_round(int x){
    if(x>=0) return (x+F/2)/F;
    return (x-F/2)/F;
}     
/***round toward zero***/
int fp_to_int(int x)            {return x/F;}
int add_fp(int x, int y)        {return x+y;}
int add_mixed(int x, int n)     {return x+n*F;}
int sub_fp(int x, int y)        {return x-y;}
int sub_mixed(int x, int n)     {return x-n*F;}
int mult_fp(int x, int y)       {return ((int64_t)x)*y/F;}
int mult_mixed(int x, int n)    {return x*n;}
int div_fp(int x, int y)        {return ((int64_t)x)*F/y;}
int div_mixed(int x, int n)     {return x/n;}