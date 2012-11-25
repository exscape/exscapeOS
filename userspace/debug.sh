# Source this file in e.g. your shell startup (~/.bash_profile or similar)

# Userspace debugging helpers
# Examples: "gu userspace/helloworld", then start qemu, and run, to have debug symbols
#           "gub userspace/helloworld main", to set a breakpoint in main and boot normally after qemu is started

function gu() {
	if [[ "$#" -ne 1 ]]; then
		echo "Usage: gu <userspace ELF file>"
		return
	fi

	i586-elf-gdb -tui --eval-command "set confirm off" \
                      --eval-command "add-symbol-file $1 0x10000000" \
                      --eval-command "set confirm on" \
                      --eval-command "continue"
}

function gub() {
	if [[ "$#" -ne 2 ]]; then
		echo "Usage: gu <userspace ELF file> <function to break in>"
		return
	fi

	i586-elf-gdb -tui --eval-command "set confirm off" \
	                  --eval-command "add-symbol-file $1 0x10000000" \
					  --eval-command "set confirm on" \
					  --eval-command "break $2" \
					  --eval-command "continue"
}
