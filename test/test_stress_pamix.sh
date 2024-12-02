#!/bin/sh -eu
# Script to test pamix for race conditions 1.0.0, Copyright (c) 2020 Ma_Sys.ma.
# For further info send an e-mail to Ma_Sys.ma@web.de.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# Dependencies: pamix build depends
#               + valgrind, tmux, arecord, mpv, dialog, coreutils

root="$(cd "$(dirname "$0")/.." && pwd)"
exe_dir="$root/build"

action="--help"
if [ $# -ge 1 ]; then
	action="$1"
	shift
fi

case "$action" in
################################################################################
## EXTERNAL FUNCTIONS ##########################################################
################################################################################

(--help) #------------------------------------------------------------[ Help ]--
	scriptname="$(basename "$0")"
	cat <<EOF
USAGE $scriptname compile|compile-debug  -- comile pamix executable
USAGE $scriptname run       <test>       -- run test without introspection
USAGE $scriptname valgrind  <test>       -- run test with Valgrind.
USAGE $scriptname installed <test>       -- run test with OS' pamix.

compile
compile-debug
	Compiles pamix. compile-debug enables debugging such that
	Valgrind can report file:line errors.

run
valgrind
installed
	Runs the given test case. This starts a tmux with four panes:
	+-----------+---------+
	| mpv/test  | control |
	| execution |         |
	+-----------+---------+
	| pamix 1   | pamix 2 |
	+-----------+---------+
	Depending on the actual action of choice, the instantiation of the
	two pamix instances is different:
	
	run:       A pamix binary from directory \`build\` is run directly.
	valgrind:  Pamix from \`build\` is invoked with valgrind.
	installed: Pamix from \$PATH is invoked (instead of \`build\`)

Parameter <test> can by one of the following:

mpv-processes
	Creates a small .wav file and repeatedly starts instances of \`mpv\` to
	play it. Expected behaviour: pamix 1 and 2 stay alive and show
	appearing and disappearing outputs in short sequence.

mpv-pause-unpause
	Creates a one minute .wav file and plays it in an \`mpv\`. Using a pipe,
	pause/unpause commands are sent to \`mpv\` in short sequence, cauinsg
	\`pamix\` to flicker due to quickly updating output information.
	It is expected that \`pamix\` instances keep running.
EOF
	;;

(compile|compile-debug) #------------------------------------------[ Compile ]--
	[ -d "$exe_dir" ] || mkdir "$exe_dir"
	if [ "$action" = compile ]; then
		cmake -S "$root" -B "$exe_dir"
	else
		cmake -DCMAKE_BUILD_TYPE=Debug -S "$root" -B "$exe_dir"
	fi
	exec cmake --build "$exe_dir" -j --clean-first 
	;;

(run|valgrind|installed) #---------------------------------------------[ Run ]--
	mkdir "pamix_test_$$"
	PAMIX_TEST_CONTROL_DIR="$(cd "pamix_test_$$" && pwd)"
	export PAMIX_TEST_CONTROL_DIR
	if [ $# = 0 ]; then
		echo ERROR: Need to specify test name to run. 1>&2
		exit 1
	fi
	if [ -n "$COLUMNS" ]; then
		export COLUMNS=$((COLUMNS / 2))
	fi
	exec tmux \
		new-session  -s pamixtest -d "exec \"$0\" \"test-$1\"" \; \
		split-window -d "exec \"$0\" \"pamix-$action\" 1"      \; \
		split-window -d "exec \"$0\" \"pamix-$action\" 2"      \; \
		split-window -d "exec \"$0\" control-ui"               \; \
		select-layout tiled                                    \; \
		select-pane -R                                         \; \
		attach -t pamixtest
	;;

################################################################################
## INTERNAL FUNCTIONS ##########################################################
################################################################################

(pamix-installed)
	cd "$PAMIX_TEST_CONTROL_DIR"
	echo $$ > "ctrl_lastpamix$1.txt"
	exec pamix
	;;

(pamix-run|pamix-valgrind)
	binary=
	for i in "$exe_dir/pamix" "$(cd "$(dirname "$0")" && pwd)/pamix" \
							"$(pwd)/pamix"; do
		if [ -x "$i" ]; then
			binary="$i"
			break
		fi
	done
	if [ -z "$binary" ]; then
		echo ERROR: Could not locate pamix binary. Press ENTER to \
								exit. 1>&2
		# variable value discarded on purpose
		# shellcheck disable=SC2034
		read -r nothing
		exit 1
	else
		echo "pamix located at $binary."
		cd "$PAMIX_TEST_CONTROL_DIR"
		echo $$ > "ctrl_lastpamix$1.txt"
		if [ "$action" = pamix-run ]; then
			exec "$binary"
		elif [ "$action" = pamix-valgrind ]; then
			exec valgrind --log-file="valgrind_pamix$1_$$.txt" \
								"$binary"
		else
			echo "ERROR: Unknown action: $action." \
						"Press ENTER to exit." 1>&2 
			# variable value discarded on purpose
			# shellcheck disable=SC2034
			read -r nothing
			exit 1
		fi
	fi
	;;

(control-ui)
	cd "$PAMIX_TEST_CONTROL_DIR"
	start="$(date)"
	echo "$start" > info_start.txt
	sleep 2
	first="$(cat "ctrl_lastpamix1.txt")"
	second="$(cat "ctrl_lastpamix2.txt")"
	printf '\033[1;36m%s\n%s\n%s\033[0m ' \
		"pamix test started at $start" \
		"first=$first, second=$second" \
		"Press ENTER to stop tests."
	read -r nothing
	end="$(date)"
	for i in "valgrind_pamix1_$first.txt" "valgrind_pamix2_$second.txt"; do
		if [ -f "$i" ]; then
			printf '\n--> pamix ui end @%s <--\n\n' "$end" >> "$i"
		fi
	done
	kill -s TERM "$first"  || echo "Failed to kill first=$first"  2>&1
	kill -s TERM "$second" || echo "Failed to kill second=$first" 2>&1
	echo "$end" > info_end.txt
	;;

(test-mpv-processes)
	cd "$PAMIX_TEST_CONTROL_DIR"
	echo "$(date) Creating test wav file..." | tee info_log.txt
	arecord -r 8000 -s 3000 test.wav >> info_log.txt 2>&1
	# this ls is for user-informational purposes only, hence find not better
	# shellcheck disable=SC2012
	ls -lsk | tee -a info_log.txt
	echo "$(date) Starting test..." | tee -a info_log.txt
	round=1
	while ! [ -f info_end.txt ]; do
		echo "$(date) Round $round..." | tee -a info_log.txt
		mpv test.wav >> info_log.txt 2>&1
		round=$((round + 1))
	done
	echo "$(date) Test finished." | tee -a info_log.txt
	;;

(test-mpv-pause-unpause)
	cd "$PAMIX_TEST_CONTROL_DIR"
	mkfifo pipe
	echo "$(date) Creating test wav file..." | tee info_log.txt
	arecord -d 60 test.wav >> info_log.txt 2>&1
	# shellcheck disable=SC2012
	ls -lsk | tee info_log.txt
	echo "$(date) Starting test..." | tee -a info_log.txt
	mpv --input-file=pipe --loop-file=inf test.wav &
	mpvpid=$!
	yes "keypress p" > pipe &
	yespid=$!
	# deliberately expand variables now
	# shellcheck disable=SC2064
	trap "kill -s TERM $mpvpid $yespid" INT TERM
	while ! [ -f info_end.txt ]; do
		sleep 1
		printf . | tee -a info_log.txt
	done
	kill -s TERM "$mpvpid" "$yespid" 2>&1 | tee -a info_log.txt
	exit 0
	echo "$(date) Test finished." | tee -a info_log.txt
	;;

(*)
	echo "Unknown action: $action. Press ENTER to exit." 1>&2
	# shellcheck disable=SC2034
	read -r nothing
	exit 1
esac
