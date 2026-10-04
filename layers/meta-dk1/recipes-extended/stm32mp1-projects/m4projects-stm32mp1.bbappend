# ST build all 25 Cube projects for the board. We only want two:
#
#   OpenAMP_TTY_echo      proves the whole coprocessor path -- toolchain,
#                         linker script, resource table, remoteproc load,
#                         IPCC mailbox, rpmsg channel -- with no code of ours
#                         in the way. Echoes on /dev/ttyRPMSG0.
#   I2C_TwoBoards_ComIT   reference for the I2C5 MSP and pin setup when the
#                         M4 starts driving the OLED.
#
# The rest is a lot of compile time for firmware we will never load.
PROJECTS_LIST = " \
    STM32MP157C-DK2/Applications/OpenAMP/OpenAMP_TTY_echo \
    STM32MP157C-DK2/Examples/I2C/I2C_TwoBoards_ComIT \
"
