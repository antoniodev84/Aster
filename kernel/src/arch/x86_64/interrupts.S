.section .text
.global x64_load_gdt
.type x64_load_gdt, @function
x64_load_gdt:
    lgdt (%rdi)
    pushq $8
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $16, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xor %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    movw $40, %ax
    ltr %ax
    ret

.macro ISR number, error=0
.global x64_isr_\number
.type x64_isr_\number, @function
x64_isr_\number:
.if !\error
    pushq $0
.endif
    pushq $\number
    jmp x64_isr_common
.endm

ISR 0, 0
ISR 1, 0
ISR 2, 0
ISR 3, 0
ISR 4, 0
ISR 5, 0
ISR 6, 0
ISR 7, 0
ISR 8, 1
ISR 9, 0
ISR 10, 1
ISR 11, 1
ISR 12, 1
ISR 13, 1
ISR 14, 1
ISR 15, 0
ISR 16, 0
ISR 17, 1
ISR 18, 0
ISR 19, 0
ISR 20, 0
ISR 21, 1
ISR 22, 0
ISR 23, 0
ISR 24, 0
ISR 25, 0
ISR 26, 0
ISR 27, 0
ISR 28, 0
ISR 29, 1
ISR 30, 1
ISR 31, 0
ISR 32, 0
ISR 33, 0
ISR 34, 0
ISR 35, 0
ISR 36, 0
ISR 37, 0
ISR 38, 0
ISR 39, 0
ISR 40, 0
ISR 41, 0
ISR 42, 0
ISR 43, 0
ISR 44, 0
ISR 45, 0
ISR 46, 0
ISR 47, 0
ISR 48, 0
ISR 49, 0
ISR 50, 0
ISR 51, 0
ISR 52, 0
ISR 53, 0
ISR 54, 0
ISR 55, 0
ISR 56, 0
ISR 57, 0
ISR 58, 0
ISR 59, 0
ISR 60, 0
ISR 61, 0
ISR 62, 0
ISR 63, 0
ISR 64, 0
ISR 65, 0
ISR 66, 0
ISR 67, 0
ISR 68, 0
ISR 69, 0
ISR 70, 0
ISR 71, 0
ISR 72, 0
ISR 73, 0
ISR 74, 0
ISR 75, 0
ISR 76, 0
ISR 77, 0
ISR 78, 0
ISR 79, 0
ISR 80, 0
ISR 81, 0
ISR 82, 0
ISR 83, 0
ISR 84, 0
ISR 85, 0
ISR 86, 0
ISR 87, 0
ISR 88, 0
ISR 89, 0
ISR 90, 0
ISR 91, 0
ISR 92, 0
ISR 93, 0
ISR 94, 0
ISR 95, 0
ISR 96, 0
ISR 97, 0
ISR 98, 0
ISR 99, 0
ISR 100, 0
ISR 101, 0
ISR 102, 0
ISR 103, 0
ISR 104, 0
ISR 105, 0
ISR 106, 0
ISR 107, 0
ISR 108, 0
ISR 109, 0
ISR 110, 0
ISR 111, 0
ISR 112, 0
ISR 113, 0
ISR 114, 0
ISR 115, 0
ISR 116, 0
ISR 117, 0
ISR 118, 0
ISR 119, 0
ISR 120, 0
ISR 121, 0
ISR 122, 0
ISR 123, 0
ISR 124, 0
ISR 125, 0
ISR 126, 0
ISR 127, 0
ISR 128, 0
ISR 129, 0
ISR 130, 0
ISR 131, 0
ISR 132, 0
ISR 133, 0
ISR 134, 0
ISR 135, 0
ISR 136, 0
ISR 137, 0
ISR 138, 0
ISR 139, 0
ISR 140, 0
ISR 141, 0
ISR 142, 0
ISR 143, 0
ISR 144, 0
ISR 145, 0
ISR 146, 0
ISR 147, 0
ISR 148, 0
ISR 149, 0
ISR 150, 0
ISR 151, 0
ISR 152, 0
ISR 153, 0
ISR 154, 0
ISR 155, 0
ISR 156, 0
ISR 157, 0
ISR 158, 0
ISR 159, 0
ISR 160, 0
ISR 161, 0
ISR 162, 0
ISR 163, 0
ISR 164, 0
ISR 165, 0
ISR 166, 0
ISR 167, 0
ISR 168, 0
ISR 169, 0
ISR 170, 0
ISR 171, 0
ISR 172, 0
ISR 173, 0
ISR 174, 0
ISR 175, 0
ISR 176, 0
ISR 177, 0
ISR 178, 0
ISR 179, 0
ISR 180, 0
ISR 181, 0
ISR 182, 0
ISR 183, 0
ISR 184, 0
ISR 185, 0
ISR 186, 0
ISR 187, 0
ISR 188, 0
ISR 189, 0
ISR 190, 0
ISR 191, 0
ISR 192, 0
ISR 193, 0
ISR 194, 0
ISR 195, 0
ISR 196, 0
ISR 197, 0
ISR 198, 0
ISR 199, 0
ISR 200, 0
ISR 201, 0
ISR 202, 0
ISR 203, 0
ISR 204, 0
ISR 205, 0
ISR 206, 0
ISR 207, 0
ISR 208, 0
ISR 209, 0
ISR 210, 0
ISR 211, 0
ISR 212, 0
ISR 213, 0
ISR 214, 0
ISR 215, 0
ISR 216, 0
ISR 217, 0
ISR 218, 0
ISR 219, 0
ISR 220, 0
ISR 221, 0
ISR 222, 0
ISR 223, 0
ISR 224, 0
ISR 225, 0
ISR 226, 0
ISR 227, 0
ISR 228, 0
ISR 229, 0
ISR 230, 0
ISR 231, 0
ISR 232, 0
ISR 233, 0
ISR 234, 0
ISR 235, 0
ISR 236, 0
ISR 237, 0
ISR 238, 0
ISR 239, 0
ISR 240, 0
ISR 241, 0
ISR 242, 0
ISR 243, 0
ISR 244, 0
ISR 245, 0
ISR 246, 0
ISR 247, 0
ISR 248, 0
ISR 249, 0
ISR 250, 0
ISR 251, 0
ISR 252, 0
ISR 253, 0
ISR 254, 0
ISR 255, 0

x64_isr_common:
    testb $3, 24(%rsp)
    jz 2f
    swapgs
2:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call x64_interrupt_dispatch
    movq %rax, %rsp
.global x64_restore_frame
x64_restore_frame:
    cmpq $256,120(%rsp)
    je x64_sysret_frame
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    testb $3, 8(%rsp)
    jz 3f
    swapgs
3:
    iretq

x64_sysret_frame:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16,%rsp
    movq 0(%rsp),%rcx
    movq 16(%rsp),%r11
    movq 24(%rsp),%rsp
    swapgs
    sysretq

.section .rodata
.balign 8
.global x64_isr_table
x64_isr_table:
    .quad x64_isr_0
    .quad x64_isr_1
    .quad x64_isr_2
    .quad x64_isr_3
    .quad x64_isr_4
    .quad x64_isr_5
    .quad x64_isr_6
    .quad x64_isr_7
    .quad x64_isr_8
    .quad x64_isr_9
    .quad x64_isr_10
    .quad x64_isr_11
    .quad x64_isr_12
    .quad x64_isr_13
    .quad x64_isr_14
    .quad x64_isr_15
    .quad x64_isr_16
    .quad x64_isr_17
    .quad x64_isr_18
    .quad x64_isr_19
    .quad x64_isr_20
    .quad x64_isr_21
    .quad x64_isr_22
    .quad x64_isr_23
    .quad x64_isr_24
    .quad x64_isr_25
    .quad x64_isr_26
    .quad x64_isr_27
    .quad x64_isr_28
    .quad x64_isr_29
    .quad x64_isr_30
    .quad x64_isr_31
    .quad x64_isr_32
    .quad x64_isr_33
    .quad x64_isr_34
    .quad x64_isr_35
    .quad x64_isr_36
    .quad x64_isr_37
    .quad x64_isr_38
    .quad x64_isr_39
    .quad x64_isr_40
    .quad x64_isr_41
    .quad x64_isr_42
    .quad x64_isr_43
    .quad x64_isr_44
    .quad x64_isr_45
    .quad x64_isr_46
    .quad x64_isr_47
    .quad x64_isr_48
    .quad x64_isr_49
    .quad x64_isr_50
    .quad x64_isr_51
    .quad x64_isr_52
    .quad x64_isr_53
    .quad x64_isr_54
    .quad x64_isr_55
    .quad x64_isr_56
    .quad x64_isr_57
    .quad x64_isr_58
    .quad x64_isr_59
    .quad x64_isr_60
    .quad x64_isr_61
    .quad x64_isr_62
    .quad x64_isr_63
    .quad x64_isr_64
    .quad x64_isr_65
    .quad x64_isr_66
    .quad x64_isr_67
    .quad x64_isr_68
    .quad x64_isr_69
    .quad x64_isr_70
    .quad x64_isr_71
    .quad x64_isr_72
    .quad x64_isr_73
    .quad x64_isr_74
    .quad x64_isr_75
    .quad x64_isr_76
    .quad x64_isr_77
    .quad x64_isr_78
    .quad x64_isr_79
    .quad x64_isr_80
    .quad x64_isr_81
    .quad x64_isr_82
    .quad x64_isr_83
    .quad x64_isr_84
    .quad x64_isr_85
    .quad x64_isr_86
    .quad x64_isr_87
    .quad x64_isr_88
    .quad x64_isr_89
    .quad x64_isr_90
    .quad x64_isr_91
    .quad x64_isr_92
    .quad x64_isr_93
    .quad x64_isr_94
    .quad x64_isr_95
    .quad x64_isr_96
    .quad x64_isr_97
    .quad x64_isr_98
    .quad x64_isr_99
    .quad x64_isr_100
    .quad x64_isr_101
    .quad x64_isr_102
    .quad x64_isr_103
    .quad x64_isr_104
    .quad x64_isr_105
    .quad x64_isr_106
    .quad x64_isr_107
    .quad x64_isr_108
    .quad x64_isr_109
    .quad x64_isr_110
    .quad x64_isr_111
    .quad x64_isr_112
    .quad x64_isr_113
    .quad x64_isr_114
    .quad x64_isr_115
    .quad x64_isr_116
    .quad x64_isr_117
    .quad x64_isr_118
    .quad x64_isr_119
    .quad x64_isr_120
    .quad x64_isr_121
    .quad x64_isr_122
    .quad x64_isr_123
    .quad x64_isr_124
    .quad x64_isr_125
    .quad x64_isr_126
    .quad x64_isr_127
    .quad x64_isr_128
    .quad x64_isr_129
    .quad x64_isr_130
    .quad x64_isr_131
    .quad x64_isr_132
    .quad x64_isr_133
    .quad x64_isr_134
    .quad x64_isr_135
    .quad x64_isr_136
    .quad x64_isr_137
    .quad x64_isr_138
    .quad x64_isr_139
    .quad x64_isr_140
    .quad x64_isr_141
    .quad x64_isr_142
    .quad x64_isr_143
    .quad x64_isr_144
    .quad x64_isr_145
    .quad x64_isr_146
    .quad x64_isr_147
    .quad x64_isr_148
    .quad x64_isr_149
    .quad x64_isr_150
    .quad x64_isr_151
    .quad x64_isr_152
    .quad x64_isr_153
    .quad x64_isr_154
    .quad x64_isr_155
    .quad x64_isr_156
    .quad x64_isr_157
    .quad x64_isr_158
    .quad x64_isr_159
    .quad x64_isr_160
    .quad x64_isr_161
    .quad x64_isr_162
    .quad x64_isr_163
    .quad x64_isr_164
    .quad x64_isr_165
    .quad x64_isr_166
    .quad x64_isr_167
    .quad x64_isr_168
    .quad x64_isr_169
    .quad x64_isr_170
    .quad x64_isr_171
    .quad x64_isr_172
    .quad x64_isr_173
    .quad x64_isr_174
    .quad x64_isr_175
    .quad x64_isr_176
    .quad x64_isr_177
    .quad x64_isr_178
    .quad x64_isr_179
    .quad x64_isr_180
    .quad x64_isr_181
    .quad x64_isr_182
    .quad x64_isr_183
    .quad x64_isr_184
    .quad x64_isr_185
    .quad x64_isr_186
    .quad x64_isr_187
    .quad x64_isr_188
    .quad x64_isr_189
    .quad x64_isr_190
    .quad x64_isr_191
    .quad x64_isr_192
    .quad x64_isr_193
    .quad x64_isr_194
    .quad x64_isr_195
    .quad x64_isr_196
    .quad x64_isr_197
    .quad x64_isr_198
    .quad x64_isr_199
    .quad x64_isr_200
    .quad x64_isr_201
    .quad x64_isr_202
    .quad x64_isr_203
    .quad x64_isr_204
    .quad x64_isr_205
    .quad x64_isr_206
    .quad x64_isr_207
    .quad x64_isr_208
    .quad x64_isr_209
    .quad x64_isr_210
    .quad x64_isr_211
    .quad x64_isr_212
    .quad x64_isr_213
    .quad x64_isr_214
    .quad x64_isr_215
    .quad x64_isr_216
    .quad x64_isr_217
    .quad x64_isr_218
    .quad x64_isr_219
    .quad x64_isr_220
    .quad x64_isr_221
    .quad x64_isr_222
    .quad x64_isr_223
    .quad x64_isr_224
    .quad x64_isr_225
    .quad x64_isr_226
    .quad x64_isr_227
    .quad x64_isr_228
    .quad x64_isr_229
    .quad x64_isr_230
    .quad x64_isr_231
    .quad x64_isr_232
    .quad x64_isr_233
    .quad x64_isr_234
    .quad x64_isr_235
    .quad x64_isr_236
    .quad x64_isr_237
    .quad x64_isr_238
    .quad x64_isr_239
    .quad x64_isr_240
    .quad x64_isr_241
    .quad x64_isr_242
    .quad x64_isr_243
    .quad x64_isr_244
    .quad x64_isr_245
    .quad x64_isr_246
    .quad x64_isr_247
    .quad x64_isr_248
    .quad x64_isr_249
    .quad x64_isr_250
    .quad x64_isr_251
    .quad x64_isr_252
    .quad x64_isr_253
    .quad x64_isr_254
    .quad x64_isr_255

.section .note.GNU-stack,"",@progbits
