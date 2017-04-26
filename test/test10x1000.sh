#!/bin/sh
for i in `seq 1 1000`
do
    echo "==================== test $i ===================="
    ./evdtest_tests -r 10 "$@"
    if [ $? -ne 0 ]; then
        echo "!!!!!!!!!!!!!!!!!!!! test error !!!!!!!!!!!!!!!!!!!!"
        exit 1
    fi
done
