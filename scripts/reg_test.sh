#!/bin/bash

REP=1
SOK="\x1B[37m[ \x1B[32mOK \x1B[37m]"
SFAIL="\x1B[37m[ \x1B[31mFAILED \x1B[37m]"
ROK=0
RFAIL=0

declare -a TESTS=(
        "./mcl_null -r 100 -w 1"
        "./mcl_null -r 100 -w 2"
        "./mcl_null -r 1000 -w 8"
        "./mcl_exec -r 100 -w 1"
        "./mcl_exec -r 100 -w 2"
        "./mcl_exec -r 512 -w 8"
        "./ocl_saxpy -s 128 -t 0"
        "./ocl_saxpy -s 1024 -t 0"
        "./ocl_saxpy -s 1024 -t 1"
        "./mcl_saxpy -s 128 -t 0 -w 1"
        "./mcl_saxpy -s 128 -t 0 -w 2"
        "./mcl_saxpy -s 1024 -t 0 -w 2"
        "./mcl_saxpy -s 128 -t 1 -w 1"
        "./mcl_saxpy -s 128 -t 2 -w 2"
        "./mcl_saxpy -s 1024 -t 2 -w 2"
        "./ocl_vadd  -r 128 -t 0"
        "./ocl_vadd  -r 1024 -t 0"
        "./ocl_vadd  -r 1024 -t 0"
        "./ocl_vadd  -r 1024 -t 1"
        "./mcl_vadd  -r 128 -t 0 -w 1"
        "./mcl_vadd  -r 128 -t 0 -w 2"
        "./mcl_vadd  -r 1024 -t 0 -w 2"
        "./mcl_vadd  -r 128 -t 1 -w 1"
        "./mcl_vadd  -r 128 -t 1 -w 2"
        "./mcl_vadd  -r 1024 -t 1 -w 2"
        "./ocl_gemm  -s 512 -t 1"
        "./ocl_gemm  -s 1024 -t 1"
        "./mcl_gemm  -s 512 -t 1"
        "./mcl_gemm  -s 1024 -t 1"
        "./mcl_err 2>/dev/null"
        "./mcl_resdata -s 64 -r 128 -t 1 -w 1"
        "./mcl_resdata -s 128 -r 128 -t 1 -w 1"
        "./mcl_resdata -s 128 -r 512 -t 1 -w 2"
        "./mcl_fft -s 128 -r 128 -t 1 -w 2"
        "./mcl_fft -s 512 -r 512 -t 1 -w 4"
    )

run_test(){
    FOUT="$DPATH/$DOUT/mcltest_$TINDEX.log"
    
    for i in `seq 1 $REP`
    do
	$1 >> $FOUT 
    done

    RES="$(grep SUCCESS $FOUT | wc -l)"
    return $RES
}

check_result(){
    if (( $1 == $REP ))
    then
	printf "$SOK"
	((ROK=ROK+1))
    else
	printf "$SFAIL"
	((RFAIL=RFAIL+1))
    fi

    printf "\n"
}

RDATE=$(date)
RHOST=$(hostname)
RSYS=$(uname)
DOUT="mclreg-$(date '+%Y%m%d-%H:%M:%S')"
DPATH="`pwd`"
TINDEX=0

printf "==============================================================\n"
printf "|                                                            |\n"
printf "|                    MCL Regression Test                     |\n"
printf "|                                                            |\n"
printf "| Date:   %-50s |\n" "$(date)"
printf "| Host:   %-50s |\n" "$(hostname)"
printf "| System: %-50s |\n" "$(uname)"
printf "=============================================================\n"

while [ -r "$1" ]
do
 case "$1" in
     -d) 
	 DPATH=$2
	 shift
	 ;;
     -r) 
	 REP=$2
	 shift
	 ;;
     *)  echo "Option $1 not recognized" 
	 exit
	 ;;     
 esac
 shift 
done

printf "Setting output directory to $DPATH/$DOUT\n" 
mkdir "$DPATH/$DOUT"

printf "Starting MCL scheduler ... "
../src/sched/mcl_sched -p rr >> $DPATH/$DOUT/reg_sched.log &
SPID=$!
if (( $? == 0 ))
then
    sleep 5
    printf "                            $SOK\n"
else
    printf "                            $FAIL\n"
fi

printf "==============================================================\n"
printf "| Running regression unit tests                              |\n"
printf "==============================================================\n"
cd ../test/

for i in "${TESTS[@]}"
do
    printf "\x1B[37m %03d %-50s" $TINDEX "$i"
    run_test "$i" $TINDEX
    check_result $?
    ((TINDEX=TINDEX + 1))
    sleep 1
done

printf "%s\n" "--------------------------------------------------------------"
printf "Terminating MCL scheduler ... "
kill -2 $SPID
printf "                         $SOK\n"

printf "==============================================================\n"
printf "| Regression test completed.                                 |\n"
printf "|     Number of successful tests:  %-2s / %-20s |\n" "$ROK" "${#TESTS[@]}"
printf "|     Number of failed tests:      %-2s / %-20s |\n" "$RFAIL" "${#TESTS[@]}"
printf "|     Number of repetitions/test:  %-25s |\n" "$REP"
printf "| Output directory: %-40s |\n" "$DPATH/$DOUT"
printf "==============================================================\n"

