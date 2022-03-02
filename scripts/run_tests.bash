#!/bin/bash

REPS=(64 128 256 512 1024)
WORKERS=(1 2 4 8 16 32 64)
SIZES=(64 128 256 512)
TYPES=(1)
OUTFILE=mclnbw

for s in ${SIZES[@]}
do
    for r in ${REPS[@]}
    do
	for t in ${TYPES[@]}
	do
	    printf "Running $s $r $t test...\n" 
	    ./mcl_fft -s $s -r $r -t $t -w 1 > $OUTFILE.$s-$r-$t.out
	    R=`grep -rie "SUCCESS" $OUTFILE.$s-$r-$t.out | wc -l`
	    if (( $R == 1 )) 
	    then
		T=`grep -rie "Test time:" $OUTFILE.$s-$r-$t.out | awk -F ' ' '{print $3}'`
	    else
		T=0
	    fi
	    printf "$s $r $t $T $R\n">>$OUTFILE.res
	done
    done
done

OUTFILE = oclnbw
for s in ${SIZES[@]}
do
    for r in ${REPS[@]}
    do
	for t in ${TYPES[@]}
	do
	    printf "Running $s $r $t test...\n" 
	    ./ocl_fft -s $s -r $r -t $t -w 1 > $OUTFILE.$s-$r-$t.out
	    R=`grep -rie "SUCCESS" $OUTFILE.$s-$r-$t.out | wc -l`
	    if (( $R == 1 )) 
	    then
		T=`grep -rie "Test time:" $OUTFILE.$s-$r-$t.out | awk -F ' ' '{print $3}'`
	    else
		T=0
	    fi
	    printf "$s $r $t $T $R\n">>$OUTFILE.res
	done
    done
done
