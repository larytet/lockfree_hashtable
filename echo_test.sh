#!/bin/bash

function echo_loop()
{
  file=echo_file_`date +%s%N`
  #echo $file
  echo > $file
  counter=1
  end=$((SECONDS+10))
  while [ $SECONDS -lt $end ]; do 
    echo $counter >>$file
    counter=$((counter+1))
  done
  tail -n 1 $file
  rm -f $file
}


CPUS=`grep -c ^processor /proc/cpuinfo`
pids=""
counter=0
while [  $counter -lt $CPUS ]; do
  echo_loop &
  pids="$pids $!"
  counter=$(($counter+1))
done

wait $pids

