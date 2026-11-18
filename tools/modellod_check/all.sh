M=/i/Projects/OpenEden-work/modellod
for pose in ground ground15 air vivec crowd; do for sh in 0 1; do for res in "1280 720" "320 180"; do
 n=${pose}_s${sh}_${res% *}; ./run.sh I:/Projects/OpenEden-work/papyrus/build I:/Projects/OpenEden-work/integ2/base $M/$n $pose $res $sh >/dev/null
 echo "$n $(python summarize.py $M/$n/frames.txt 200)" >> $M/results.txt
 rm -rf $M/$n/user $M/$n/config/*.xml
done; done; done
echo ALLDONE >> $M/results.txt
