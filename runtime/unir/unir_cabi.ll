; ModuleID = 'unir_cabi'
source_filename = "llvm-link"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

module asm ".intel_syntax"
module asm ".pushsection .text._RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack,\22ax\22, @progbits"
module asm ".balign 4"
module asm ".globl _RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack"
module asm ".type _RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack, @function"
module asm "_RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack:"
module asm "            .cfi_startproc"
module asm "            push  rbp"
module asm "            .cfi_adjust_cfa_offset 8"
module asm "            .cfi_offset rbp, -16"
module asm "            mov   rbp, rsp"
module asm "            .cfi_def_cfa_register rbp"
module asm "            mov    r11, rax        // duplicate rax as we're clobbering r11"
module asm "            // Main loop, taken in one page increments. We're decrementing rsp by"
module asm "            // a page each time until there's less than a page remaining. We're"
module asm "            // guaranteed that this function isn't called unless there's more than a"
module asm "            // page needed."
module asm "            //"
module asm "            // Note that we're also testing against `[rsp + 8]` to account for the 8"
module asm "            // bytes pushed on the stack originally with our return address. Using"
module asm "            // `[rsp + 8]` simulates us testing the stack pointer in the caller's"
module asm "            // context."
module asm "            // It's usually called when rax >= 0x1000, but that's not always true."
module asm "            // Dynamic stack allocation, which is needed to implement unsized"
module asm "            // rvalues, triggers stackprobe even if rax < 0x1000."
module asm "            // Thus we have to check r11 first to avoid segfault."
module asm "            cmp    r11, 0x1000"
module asm "            jna    3f"
module asm "        2:"
module asm "            sub    rsp, 0x1000"
module asm "            test   qword ptr [rsp + 8], rsp"
module asm "            sub    r11, 0x1000"
module asm "            cmp    r11, 0x1000"
module asm "            ja     2b"
module asm "        3:"
module asm "            // Finish up the last remaining stack space requested, getting the last"
module asm "            // bits out of r11"
module asm "            sub    rsp, r11"
module asm "            test   qword ptr [rsp + 8], rsp"
module asm "            // Restore the stack pointer to what it previously was when entering"
module asm "            // this function. The caller will readjust the stack pointer after we"
module asm "            // return."
module asm "            add    rsp, rax"
module asm "            leave"
module asm "            .cfi_def_cfa_register rsp"
module asm "            .cfi_adjust_cfa_offset -8"
module asm "    "
module asm "       ret"
module asm "            .cfi_endproc"
module asm "    "
module asm ".Lfunc_end__RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack:"
module asm ".size _RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack, . - _RNvCs1Y7DaGC1cwg_7___rustc17___rust_probestack"
module asm ".popsection"
module asm ".att_syntax"

@alloc_ed2c01013b845a2275ba463d39b0766f.10 = private unnamed_addr constant [11 x i8] c"<redacted>\00", align 1
@alloc_11e2992a5b9c8f277d4a00890b54e14e.9 = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_ed2c01013b845a2275ba463d39b0766f.10, [16 x i8] c"\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00" }>, align 8
@alloc_850f178d3cd0d4307d8d45b841b2c2ad = private unnamed_addr constant [6 x i8] c"\C0\02: \C0\00", align 1
@alloc_a434a7e153ac922489f2ed192aeda5a5 = private unnamed_addr constant [55 x i8] c" index out of bounds: the len is \C0\12 but the index is \C0\00", align 1
@alloc_7ef6fe6e5749e409bd35c794f395407d = private unnamed_addr constant [200 x i8] c"00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899", align 1
@alloc_f60af4dda577965ed7990eef71c78b71 = private unnamed_addr constant [55 x i8] c"\10range end index \C0\22 out of range for slice of length \C0\00", align 1
@alloc_bfe1e99c4b1e5a3c7be66967fd889b22 = private unnamed_addr constant [40 x i8] c"\16slice index starts at \C0\0D but ends at \C0\00", align 1
@alloc_9fe2a66cc3026f503dfb4e2696ddcf9c = private unnamed_addr constant [57 x i8] c"\12range start index \C0\22 out of range for slice of length \C0\00", align 1
@vtable.0.73 = private unnamed_addr constant <{ [24 x i8], ptr }> <{ [24 x i8] c"\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00", ptr @_RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt }>, align 8
@alloc_27f7ec91e8c14f79b1c7bc4a57a83efa = private unnamed_addr constant [19 x i8] c"len \E2\89\A4 max_payload", align 1
@alloc_ed2c01013b845a2275ba463d39b0766f.75 = private unnamed_addr constant [11 x i8] c"<redacted>\00", align 1
@alloc_11e2992a5b9c8f277d4a00890b54e14e.74 = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_ed2c01013b845a2275ba463d39b0766f.75, [16 x i8] c"\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00" }>, align 8
@alloc_85fc59111fd0cef7ef4093da3840b035 = private unnamed_addr constant [8 x i8] zeroinitializer, align 1
@alloc_cc193f9e0c79ce36721b88a4026725d4 = private unnamed_addr constant [10 x i8] c"\00\01\02\03\04\05\06\07\08\09", align 1
@alloc_311320cd5b7ccf232ac56b395a8125b7 = private unnamed_addr constant [5 x i8] c"Short", align 1
@alloc_2debff00161543495d219822aebef15f = private unnamed_addr constant [13 x i8] c"ReservedFlags", align 1
@alloc_77bfe3df11e483c5c3ba2d820506dfe2 = private unnamed_addr constant [12 x i8] c"ReservedBits", align 1
@alloc_375412519fb05804ad36ade3db62b364 = private unnamed_addr constant [13 x i8] c"ReservedCause", align 1
@alloc_739e73de2a50a191b55f7ee493cf553a = private unnamed_addr constant [13 x i8] c"ReservedState", align 1
@alloc_cfab2fc7164d0c19d0a0c55e8a3c7bdf = private unnamed_addr constant [17 x i8] c"ReservedPredicate", align 1
@alloc_7a2416141cbef58c1d3348b25fafebee = private unnamed_addr constant [18 x i8] c"ReservedCoordinate", align 1
@alloc_81f298069b1965e652374858770a2ec6 = private unnamed_addr constant [19 x i8] c"LevelWithoutDurable", align 1
@alloc_cac7ecd9942a9caec256ea67c7c9e15b = private unnamed_addr constant [14 x i8] c"LengthOverflow", align 1
@switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt = private unnamed_addr constant [9 x i64] [i64 5, i64 13, i64 12, i64 13, i64 13, i64 17, i64 18, i64 19, i64 14], align 8
@switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel = private unnamed_addr constant [9 x i32] [i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_311320cd5b7ccf232ac56b395a8125b7 to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_2debff00161543495d219822aebef15f to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_77bfe3df11e483c5c3ba2d820506dfe2 to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_375412519fb05804ad36ade3db62b364 to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_739e73de2a50a191b55f7ee493cf553a to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_cfab2fc7164d0c19d0a0c55e8a3c7bdf to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_7a2416141cbef58c1d3348b25fafebee to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_81f298069b1965e652374858770a2ec6 to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32), i32 trunc (i64 sub (i64 ptrtoint (ptr @alloc_cac7ecd9942a9caec256ea67c7c9e15b to i64), i64 ptrtoint (ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel to i64)) to i32)], align 4
@switch.table.unir_producer_write = private unnamed_addr constant [6 x i64] [i64 -1, i64 -2, i64 -3, i64 -4, i64 -5, i64 0], align 8
@alloc_ed2c01013b845a2275ba463d39b0766f.84 = private unnamed_addr constant [11 x i8] c"<redacted>\00", align 1
@alloc_11e2992a5b9c8f277d4a00890b54e14e.83 = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_ed2c01013b845a2275ba463d39b0766f.84, [16 x i8] c"\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00" }>, align 8
@alloc_cc193f9e0c79ce36721b88a4026725d4.109 = private unnamed_addr constant [10 x i8] c"\00\01\02\03\04\05\06\07\08\09", align 1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #0

; Function Attrs: nounwind nonlazybind uwtable
declare void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() unnamed_addr #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write)
declare void @llvm.assume(i1 noundef) #2

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite)
declare void @llvm.experimental.noalias.scope.decl(metadata) #4

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.umax.i64(i64, i64) #5

; Function Attrs: cold minsize noreturn nounwind nonlazybind optsize uwtable
define internal void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef range(i64 1, -9223372036854775807) %layout.0, i64 noundef %layout.1) unnamed_addr #6 {
start:
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc26___rust_alloc_error_handler(i64 noundef %layout.1, i64 noundef %layout.0) #21
  unreachable
}

; Function Attrs: noreturn nounwind nonlazybind uwtable
declare void @_RNvCs1Y7DaGC1cwg_7___rustc26___rust_alloc_error_handler(i64 noundef, i64 noundef) unnamed_addr #7

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.umin.i64(i64, i64) #5

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umin.i32(i32, i32) #5

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: read)
declare ptr @llvm.load.relative.i64(ptr, i64) #8

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXs1i_NtCscliFh4jUES5_4core3fmtReNtB6_7Display3fmtB8_(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %f) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load i64, ptr %0, align 8, !noundef !3
  %_0.i = tail call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter3pad(ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %f, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_3.0, i64 noundef %_3.1) #17
  ret i1 %_0.i
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXs1g_NtCscliFh4jUES5_4core3fmtRDNtB6_5DebugEL_Bx_3fmtB8_(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, ptr noalias noundef align 8 dereferenceable(24) %f) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load ptr, ptr %0, align 8, !nonnull !3, !align !5, !noundef !3
  %1 = getelementptr inbounds nuw i8, ptr %_3.1, i64 24
  %2 = load ptr, ptr %1, align 8, !invariant.load !3, !nonnull !3
  %_0 = tail call noundef zeroext i1 %2(ptr noundef nonnull align 1 %_3.0, ptr noalias noundef nonnull align 8 dereferenceable(24) %f) #22
  ret i1 %_0
}

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull %0, ptr noundef nonnull %1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %2) unnamed_addr #9 {
start:
  %pi = alloca [24 x i8], align 8
  %fmt = alloca [16 x i8], align 8
  store ptr %0, ptr %fmt, align 8
  %3 = getelementptr inbounds nuw i8, ptr %fmt, i64 8
  store ptr %1, ptr %3, align 8
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %pi)
  store ptr %fmt, ptr %pi, align 8
  %4 = getelementptr inbounds nuw i8, ptr %pi, i64 8
  store ptr %2, ptr %4, align 8
  %5 = getelementptr inbounds nuw i8, ptr %pi, i64 16
  store i8 1, ptr %5, align 8
  %6 = getelementptr inbounds nuw i8, ptr %pi, i64 17
  store i8 0, ptr %6, align 1
  call void @_RNvCs1Y7DaGC1cwg_7___rustc17rust_begin_unwind(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %pi) #21
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter3pad(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %self, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) unnamed_addr #1 {
start:
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_4 = load i32, ptr %2, align 8, !noundef !3
  %_3 = and i32 %_4, 402653184
  %3 = icmp eq i32 %_3, 0
  br i1 %3, label %bb1, label %bb3

bb1:                                              ; preds = %start
  %_26.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_26.1 = load ptr, ptr %4, align 8, !nonnull !3, !align !5, !noundef !3
  %5 = getelementptr inbounds nuw i8, ptr %_26.1, i64 24
  %6 = load ptr, ptr %5, align 8, !invariant.load !3, !nonnull !3
  %7 = tail call noundef zeroext i1 %6(ptr noundef nonnull align 1 %_26.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) #22
  br label %bb15

bb3:                                              ; preds = %start
  %_29 = and i32 %_4, 268435456
  %8 = icmp eq i32 %_29, 0
  br i1 %8, label %bb17, label %bb16

bb15:                                             ; preds = %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, %bb27, %bb17.i, %bb12, %bb1
  %_0.sroa.0.0.shrunk = phi i1 [ %7, %bb1 ], [ %_7.i, %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit ], [ %86, %bb12 ], [ true, %bb27 ], [ true, %bb17.i ]
  ret i1 %_0.sroa.0.0.shrunk

bb17:                                             ; preds = %bb3
  %_16.i = icmp ult i64 %1, 32
  br i1 %_16.i, label %bb8.i, label %bb9.i

bb9.i:                                            ; preds = %bb17
  %9 = tail call noundef i64 @_RNvNtNtCscliFh4jUES5_4core3str5count14do_count_chars(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) #17
  br label %bb7

bb8.i:                                            ; preds = %bb17
  %10 = icmp samesign eq i64 %1, 0
  br i1 %10, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i.preheader

bb9.i.i.i.i.preheader:                            ; preds = %bb8.i
  %min.iters.check = icmp ult i64 %1, 4
  br i1 %min.iters.check, label %bb9.i.i.i.i.preheader30, label %vector.ph

vector.ph:                                        ; preds = %bb9.i.i.i.i.preheader
  %n.vec = and i64 %1, 28
  %11 = getelementptr inbounds nuw i8, ptr %0, i64 2
  %wide.load = load <2 x i8>, ptr %0, align 1, !alias.scope !6, !noalias !13
  %wide.load29 = load <2 x i8>, ptr %11, align 1, !alias.scope !6, !noalias !13
  %12 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %13 = icmp sgt <2 x i8> %wide.load29, splat (i8 -65)
  %14 = zext <2 x i1> %12 to <2 x i64>
  %15 = zext <2 x i1> %13 to <2 x i64>
  %16 = icmp eq i64 %n.vec, 4
  br i1 %16, label %middle.block, label %vector.body.1, !llvm.loop !16

vector.body.1:                                    ; preds = %vector.ph
  %17 = getelementptr inbounds nuw i8, ptr %0, i64 4
  %18 = getelementptr inbounds nuw i8, ptr %0, i64 6
  %wide.load.1 = load <2 x i8>, ptr %17, align 1, !alias.scope !6, !noalias !13
  %wide.load29.1 = load <2 x i8>, ptr %18, align 1, !alias.scope !6, !noalias !13
  %19 = icmp sgt <2 x i8> %wide.load.1, splat (i8 -65)
  %20 = icmp sgt <2 x i8> %wide.load29.1, splat (i8 -65)
  %21 = zext <2 x i1> %19 to <2 x i64>
  %22 = zext <2 x i1> %20 to <2 x i64>
  %23 = add nuw nsw <2 x i64> %14, %21
  %24 = add nuw nsw <2 x i64> %15, %22
  %25 = icmp eq i64 %n.vec, 8
  br i1 %25, label %middle.block, label %vector.body.2, !llvm.loop !16

vector.body.2:                                    ; preds = %vector.body.1
  %26 = getelementptr inbounds nuw i8, ptr %0, i64 8
  %27 = getelementptr inbounds nuw i8, ptr %0, i64 10
  %wide.load.2 = load <2 x i8>, ptr %26, align 1, !alias.scope !6, !noalias !13
  %wide.load29.2 = load <2 x i8>, ptr %27, align 1, !alias.scope !6, !noalias !13
  %28 = icmp sgt <2 x i8> %wide.load.2, splat (i8 -65)
  %29 = icmp sgt <2 x i8> %wide.load29.2, splat (i8 -65)
  %30 = zext <2 x i1> %28 to <2 x i64>
  %31 = zext <2 x i1> %29 to <2 x i64>
  %32 = add nuw nsw <2 x i64> %23, %30
  %33 = add nuw nsw <2 x i64> %24, %31
  %34 = icmp eq i64 %n.vec, 12
  br i1 %34, label %middle.block, label %vector.body.3, !llvm.loop !16

vector.body.3:                                    ; preds = %vector.body.2
  %35 = getelementptr inbounds nuw i8, ptr %0, i64 12
  %36 = getelementptr inbounds nuw i8, ptr %0, i64 14
  %wide.load.3 = load <2 x i8>, ptr %35, align 1, !alias.scope !6, !noalias !13
  %wide.load29.3 = load <2 x i8>, ptr %36, align 1, !alias.scope !6, !noalias !13
  %37 = icmp sgt <2 x i8> %wide.load.3, splat (i8 -65)
  %38 = icmp sgt <2 x i8> %wide.load29.3, splat (i8 -65)
  %39 = zext <2 x i1> %37 to <2 x i64>
  %40 = zext <2 x i1> %38 to <2 x i64>
  %41 = add nuw nsw <2 x i64> %32, %39
  %42 = add nuw nsw <2 x i64> %33, %40
  %43 = icmp eq i64 %n.vec, 16
  br i1 %43, label %middle.block, label %vector.body.4, !llvm.loop !16

vector.body.4:                                    ; preds = %vector.body.3
  %44 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %45 = getelementptr inbounds nuw i8, ptr %0, i64 18
  %wide.load.4 = load <2 x i8>, ptr %44, align 1, !alias.scope !6, !noalias !13
  %wide.load29.4 = load <2 x i8>, ptr %45, align 1, !alias.scope !6, !noalias !13
  %46 = icmp sgt <2 x i8> %wide.load.4, splat (i8 -65)
  %47 = icmp sgt <2 x i8> %wide.load29.4, splat (i8 -65)
  %48 = zext <2 x i1> %46 to <2 x i64>
  %49 = zext <2 x i1> %47 to <2 x i64>
  %50 = add nuw nsw <2 x i64> %41, %48
  %51 = add nuw nsw <2 x i64> %42, %49
  %52 = icmp eq i64 %n.vec, 20
  br i1 %52, label %middle.block, label %vector.body.5, !llvm.loop !16

vector.body.5:                                    ; preds = %vector.body.4
  %53 = getelementptr inbounds nuw i8, ptr %0, i64 20
  %54 = getelementptr inbounds nuw i8, ptr %0, i64 22
  %wide.load.5 = load <2 x i8>, ptr %53, align 1, !alias.scope !6, !noalias !13
  %wide.load29.5 = load <2 x i8>, ptr %54, align 1, !alias.scope !6, !noalias !13
  %55 = icmp sgt <2 x i8> %wide.load.5, splat (i8 -65)
  %56 = icmp sgt <2 x i8> %wide.load29.5, splat (i8 -65)
  %57 = zext <2 x i1> %55 to <2 x i64>
  %58 = zext <2 x i1> %56 to <2 x i64>
  %59 = add nuw nsw <2 x i64> %50, %57
  %60 = add nuw nsw <2 x i64> %51, %58
  %61 = icmp eq i64 %n.vec, 24
  br i1 %61, label %middle.block, label %vector.body.6, !llvm.loop !16

vector.body.6:                                    ; preds = %vector.body.5
  %62 = getelementptr inbounds nuw i8, ptr %0, i64 24
  %63 = getelementptr inbounds nuw i8, ptr %0, i64 26
  %wide.load.6 = load <2 x i8>, ptr %62, align 1, !alias.scope !6, !noalias !13
  %wide.load29.6 = load <2 x i8>, ptr %63, align 1, !alias.scope !6, !noalias !13
  %64 = icmp sgt <2 x i8> %wide.load.6, splat (i8 -65)
  %65 = icmp sgt <2 x i8> %wide.load29.6, splat (i8 -65)
  %66 = zext <2 x i1> %64 to <2 x i64>
  %67 = zext <2 x i1> %65 to <2 x i64>
  %68 = add nuw nsw <2 x i64> %59, %66
  %69 = add nuw nsw <2 x i64> %60, %67
  br label %middle.block

middle.block:                                     ; preds = %vector.body.6, %vector.body.5, %vector.body.4, %vector.body.3, %vector.body.2, %vector.body.1, %vector.ph
  %.lcssa31 = phi <2 x i64> [ %14, %vector.ph ], [ %23, %vector.body.1 ], [ %32, %vector.body.2 ], [ %41, %vector.body.3 ], [ %50, %vector.body.4 ], [ %59, %vector.body.5 ], [ %68, %vector.body.6 ]
  %.lcssa = phi <2 x i64> [ %15, %vector.ph ], [ %24, %vector.body.1 ], [ %33, %vector.body.2 ], [ %42, %vector.body.3 ], [ %51, %vector.body.4 ], [ %60, %vector.body.5 ], [ %69, %vector.body.6 ]
  %bin.rdx = add <2 x i64> %.lcssa, %.lcssa31
  %70 = tail call i64 @llvm.vector.reduce.add.v2i64(<2 x i64> %bin.rdx)
  %cmp.n = icmp eq i64 %1, %n.vec
  br i1 %cmp.n, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i.preheader30

bb9.i.i.i.i.preheader30:                          ; preds = %middle.block, %bb9.i.i.i.i.preheader
  %i.sroa.0.0.i.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.i.preheader ], [ %n.vec, %middle.block ]
  %init.sroa.0.0.i.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.i.preheader ], [ %70, %middle.block ]
  br label %bb9.i.i.i.i

bb9.i.i.i.i:                                      ; preds = %bb9.i.i.i.i, %bb9.i.i.i.i.preheader30
  %i.sroa.0.0.i.i.i.i = phi i64 [ %_21.i.i.i.i, %bb9.i.i.i.i ], [ %i.sroa.0.0.i.i.i.i.ph, %bb9.i.i.i.i.preheader30 ]
  %init.sroa.0.0.i.i.i.i = phi i64 [ %_4.0.i.i.i.i.i.i, %bb9.i.i.i.i ], [ %init.sroa.0.0.i.i.i.i.ph, %bb9.i.i.i.i.preheader30 ]
  %_37.i.i.i.i = getelementptr inbounds nuw i8, ptr %0, i64 %i.sroa.0.0.i.i.i.i
  %byte.i.i.i.i.i.i.i = load i8, ptr %_37.i.i.i.i, align 1, !alias.scope !6, !noalias !13, !noundef !3
  %_4.i.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i.i, %_0.i.i.i.i.i.i
  %_21.i.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i.i, 1
  %_22.i.i.i.i = icmp eq i64 %_21.i.i.i.i, %1
  br i1 %_22.i.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i, !llvm.loop !19

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i: ; preds = %bb9.i.i.i.i, %middle.block, %bb8.i
  %_0.sroa.0.0.i.i.i.i = phi i64 [ 0, %bb8.i ], [ %70, %middle.block ], [ %_4.0.i.i.i.i.i.i, %bb9.i.i.i.i ]
  %_3.i.i.i.i = icmp ule i64 %_0.sroa.0.0.i.i.i.i, %1
  tail call void @llvm.assume(i1 %_3.i.i.i.i)
  br label %bb7

bb16:                                             ; preds = %bb3
  %71 = getelementptr inbounds nuw i8, ptr %self, i64 22
  %_31 = load i16, ptr %71, align 2, !noundef !3
  %_38 = getelementptr inbounds nuw i8, ptr %0, i64 %1
  %_11 = zext i16 %_31 to i64
  %.not = icmp eq i16 %_31, 0
  br i1 %.not, label %bb5, label %bb1.i

bb7:                                              ; preds = %bb5, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, %bb9.i
  %char_count.sroa.0.0 = phi i64 [ %82, %bb5 ], [ %_0.sroa.0.0.i.i.i.i, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i ], [ %9, %bb9.i ]
  %s.sroa.8.0 = phi i64 [ %iter.sroa.10.1, %bb5 ], [ %1, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i ], [ %1, %bb9.i ]
  %72 = getelementptr inbounds nuw i8, ptr %self, i64 20
  %_18 = load i16, ptr %72, align 4, !noundef !3
  %_17 = zext i16 %_18 to i64
  %_16 = icmp ult i64 %char_count.sroa.0.0, %_17
  br i1 %_16, label %bb8, label %bb12

bb1.i:                                            ; preds = %bb3.i, %bb16
  %iter.sroa.10.2 = phi i64 [ %79, %bb3.i ], [ 0, %bb16 ]
  %_13.i19.i.i8.i = phi ptr [ %_25.i.i, %bb3.i ], [ %0, %bb16 ]
  %init.sroa.0.0.i = phi i64 [ %80, %bb3.i ], [ %_11, %bb16 ]
  %_6.i.i.i.i = icmp eq ptr %_13.i19.i.i8.i, %_38
  br i1 %_6.i.i.i.i, label %bb5, label %bb14.i.i.i

bb14.i.i.i:                                       ; preds = %bb1.i
  %_13.i.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i19.i.i8.i, i64 1
  %x.i.i.i = load i8, ptr %_13.i19.i.i8.i, align 1, !noalias !20, !noundef !3
  %_6.i.i.i = icmp sgt i8 %x.i.i.i, -1
  br i1 %_6.i.i.i, label %bb3.i.i.i, label %bb4.i.i.i

bb4.i.i.i:                                        ; preds = %bb14.i.i.i
  %_30.i.i.i = and i8 %x.i.i.i, 31
  %init.i.i.i = zext nneg i8 %_30.i.i.i to i32
  %_6.i3.i.i.i = icmp ne ptr %_13.i.i.i.i, %_38
  tail call void @llvm.assume(i1 %_6.i3.i.i.i)
  %_13.i5.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i19.i.i8.i, i64 2
  %y.i.i.i = load i8, ptr %_13.i.i.i.i, align 1, !noalias !20, !noundef !3
  %_33.i.i.i = shl nuw nsw i32 %init.i.i.i, 6
  %_35.i.i.i = and i8 %y.i.i.i, 63
  %_34.i.i.i = zext nneg i8 %_35.i.i.i to i32
  %73 = or disjoint i32 %_33.i.i.i, %_34.i.i.i
  %_13.i.i.i = icmp samesign ugt i8 %x.i.i.i, -33
  br i1 %_13.i.i.i, label %bb6.i.i.i, label %bb3.i

bb3.i.i.i:                                        ; preds = %bb14.i.i.i
  %_7.i.i.i = zext nneg i8 %x.i.i.i to i32
  br label %bb3.i

bb6.i.i.i:                                        ; preds = %bb4.i.i.i
  %_6.i10.i.i.i = icmp ne ptr %_13.i5.i.i.i, %_38
  tail call void @llvm.assume(i1 %_6.i10.i.i.i)
  %_13.i12.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i19.i.i8.i, i64 3
  %z.i.i.i = load i8, ptr %_13.i5.i.i.i, align 1, !noalias !20, !noundef !3
  %_38.i.i.i = shl nuw nsw i32 %_34.i.i.i, 6
  %_40.i.i.i = and i8 %z.i.i.i, 63
  %_39.i.i.i = zext nneg i8 %_40.i.i.i to i32
  %y_z.i.i.i = or disjoint i32 %_38.i.i.i, %_39.i.i.i
  %_20.i.i.i = shl nuw nsw i32 %init.i.i.i, 12
  %74 = or disjoint i32 %y_z.i.i.i, %_20.i.i.i
  %_21.i.i.i = icmp samesign ugt i8 %x.i.i.i, -17
  br i1 %_21.i.i.i, label %bb8.i.i.i, label %bb3.i

bb8.i.i.i:                                        ; preds = %bb6.i.i.i
  %_6.i17.i.i.i = icmp ne ptr %_13.i12.i.i.i, %_38
  tail call void @llvm.assume(i1 %_6.i17.i.i.i)
  %_13.i19.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i19.i.i8.i, i64 4
  %w.i.i.i = load i8, ptr %_13.i12.i.i.i, align 1, !noalias !20, !noundef !3
  %_26.i.i.i = shl nuw nsw i32 %init.i.i.i, 18
  %_25.i.i.i = and i32 %_26.i.i.i, 1835008
  %_43.i.i.i = shl nuw nsw i32 %y_z.i.i.i, 6
  %_45.i.i.i = and i8 %w.i.i.i, 63
  %_44.i.i.i = zext nneg i8 %_45.i.i.i to i32
  %_27.i.i.i = or disjoint i32 %_43.i.i.i, %_44.i.i.i
  %75 = or disjoint i32 %_27.i.i.i, %_25.i.i.i
  br label %bb3.i

bb3.i:                                            ; preds = %bb8.i.i.i, %bb6.i.i.i, %bb3.i.i.i, %bb4.i.i.i
  %_25.i.i = phi ptr [ %_13.i5.i.i.i, %bb4.i.i.i ], [ %_13.i12.i.i.i, %bb6.i.i.i ], [ %_13.i19.i.i.i, %bb8.i.i.i ], [ %_13.i.i.i.i, %bb3.i.i.i ]
  %_0.sroa.4.0.i.ph.i.i = phi i32 [ %73, %bb4.i.i.i ], [ %74, %bb6.i.i.i ], [ %75, %bb8.i.i.i ], [ %_7.i.i.i, %bb3.i.i.i ]
  %76 = ptrtoint ptr %_13.i19.i.i8.i to i64
  %77 = icmp samesign ult i32 %_0.sroa.4.0.i.ph.i.i, 1114112
  tail call void @llvm.assume(i1 %77)
  %78 = ptrtoint ptr %_25.i.i to i64
  %_7.i.i = sub i64 %78, %76
  %79 = add i64 %_7.i.i, %iter.sroa.10.2
  %80 = add nsw i64 %init.sroa.0.0.i, -1
  %81 = icmp eq i64 %80, 0
  br i1 %81, label %bb5, label %bb1.i

bb5:                                              ; preds = %bb3.i, %bb1.i, %bb16
  %iter.sroa.10.1 = phi i64 [ 0, %bb16 ], [ %iter.sroa.10.2, %bb1.i ], [ %79, %bb3.i ]
  %remaining.sroa.0.0 = phi i64 [ 0, %bb16 ], [ %init.sroa.0.0.i, %bb1.i ], [ 0, %bb3.i ]
  %82 = sub nsw i64 %_11, %remaining.sroa.0.0
  br label %bb7

bb12:                                             ; preds = %bb7
  %_28.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %83 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_28.1 = load ptr, ptr %83, align 8, !nonnull !3, !align !5, !noundef !3
  %84 = getelementptr inbounds nuw i8, ptr %_28.1, i64 24
  %85 = load ptr, ptr %84, align 8, !invariant.load !3, !nonnull !3
  %86 = tail call noundef zeroext i1 %85(ptr noundef nonnull align 1 %_28.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %s.sroa.8.0) #22
  br label %bb15

bb8:                                              ; preds = %bb7
  %_23 = trunc nuw i64 %char_count.sroa.0.0 to i16
  %_21 = sub i16 %_18, %_23
  tail call void @llvm.experimental.noalias.scope.decl(metadata !27)
  %_14.i = lshr i32 %_4, 29
  %87 = and i32 %_14.i, 3
  %_2.i11.i = and i32 %_4, 2097151
  %88 = icmp samesign ult i32 %_2.i11.i, 1114112
  tail call void @llvm.assume(i1 %88)
  switch i32 %87, label %default.unreachable [
    i32 0, label %bb6.i
    i32 1, label %bb12.i
    i32 2, label %bb11.i
    i32 3, label %bb6.i
  ]

default.unreachable:                              ; preds = %bb8
  unreachable

bb12.i:                                           ; preds = %bb8
  br label %bb6.i

bb11.i:                                           ; preds = %bb8
  %89 = lshr i16 %_21, 1
  br label %bb6.i

bb6.i:                                            ; preds = %bb11.i, %bb12.i, %bb8, %bb8
  %padding_left.sroa.0.0.i = phi i16 [ %89, %bb11.i ], [ %_21, %bb12.i ], [ 0, %bb8 ], [ 0, %bb8 ]
  %_13.0.i = load ptr, ptr %self, align 8, !alias.scope !27, !nonnull !3, !align !4
  %90 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i = load ptr, ptr %90, align 8, !alias.scope !27, !nonnull !3, !align !5
  %91 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 32
  br label %bb7.i

bb7.i:                                            ; preds = %bb17.i, %bb6.i
  %iter.sroa.0.0.i = phi i16 [ 0, %bb6.i ], [ %_17.i, %bb17.i ]
  %exitcond.not.i = icmp eq i16 %iter.sroa.0.0.i, %padding_left.sroa.0.0.i
  br i1 %exitcond.not.i, label %bb27, label %bb17.i

bb17.i:                                           ; preds = %bb7.i
  %_17.i = add i16 %iter.sroa.0.0.i, 1
  %92 = load ptr, ptr %91, align 8, !invariant.load !3, !noalias !27, !nonnull !3
  %_10.i = tail call noundef zeroext i1 %92(ptr noundef nonnull align 1 %_13.0.i, i32 noundef %_2.i11.i) #22, !noalias !27
  br i1 %_10.i, label %bb15, label %bb7.i

bb27:                                             ; preds = %bb7.i
  %_12.i = sub i16 %_21, %padding_left.sroa.0.0.i
  %93 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 24
  %94 = load ptr, ptr %93, align 8, !invariant.load !3, !nonnull !3
  %_25 = tail call noundef zeroext i1 %94(ptr noundef nonnull align 1 %_13.0.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %s.sroa.8.0) #22
  br i1 %_25, label %bb15, label %bb1.i18

bb1.i18:                                          ; preds = %bb8.i21, %bb27
  %iter.sroa.0.0.i19 = phi i16 [ %_8.i, %bb8.i21 ], [ 0, %bb27 ]
  %exitcond.not.i20 = icmp eq i16 %iter.sroa.0.0.i19, %_12.i
  br i1 %exitcond.not.i20, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb8.i21

bb8.i21:                                          ; preds = %bb1.i18
  %_8.i = add i16 %iter.sroa.0.0.i19, 1
  %95 = load ptr, ptr %91, align 8, !invariant.load !3, !noalias !30, !nonnull !3
  %_4.i = tail call noundef zeroext i1 %95(ptr noundef nonnull align 1 %_13.0.i, i32 noundef range(i32 0, 1114112) %_2.i11.i) #22, !noalias !30
  br i1 %_4.i, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb1.i18

_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit: ; preds = %bb8.i21, %bb1.i18
  %iter.sroa.0.0.i19.lcssa = phi i16 [ %_12.i, %bb1.i18 ], [ %iter.sroa.0.0.i19, %bb8.i21 ]
  %_7.i = icmp ult i16 %iter.sroa.0.0.i19.lcssa, %_12.i
  br label %bb15
}

; Function Attrs: nofree norecurse nosync nounwind nonlazybind memory(argmem: read, inaccessiblemem: write) uwtable
define internal noundef i64 @_RNvNtNtCscliFh4jUES5_4core3str5count14do_count_chars(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %s.0, i64 noundef %s.1) unnamed_addr #10 {
start:
  %addr.i.i = ptrtoint ptr %s.0 to i64
  %_9.i.i = add i64 %addr.i.i, 7
  %aligned_address.i.i = and i64 %_9.i.i, -8
  %byte_offset.i.i = sub i64 %aligned_address.i.i, %addr.i.i
  %_12.i.i = icmp ult i64 %byte_offset.i.i, 8
  tail call void @llvm.assume(i1 %_12.i.i)
  %_6.i = icmp samesign ugt i64 %byte_offset.i.i, %s.1
  br i1 %_6.i, label %bb19, label %_RINvMNtCscliFh4jUES5_4core5sliceSh8align_tojEB5_.exit

_RINvMNtCscliFh4jUES5_4core5sliceSh8align_tojEB5_.exit: ; preds = %start
  %_13.i.i = getelementptr inbounds nuw i8, ptr %s.0, i64 %byte_offset.i.i
  %_14.i.i = sub nuw nsw i64 %s.1, %byte_offset.i.i
  %_91.i.i = lshr i64 %_14.i.i, 3
  %ts_len.i.i = and i64 %_14.i.i, 7
  %_21.i = and i64 %_14.i.i, 9223372036854775800
  %_19.i = getelementptr inbounds nuw i8, ptr %_13.i.i, i64 %_21.i
  %0 = icmp ult i64 %_14.i.i, 8
  br i1 %0, label %bb19, label %bb21, !prof !33

bb21:                                             ; preds = %_RINvMNtCscliFh4jUES5_4core5sliceSh8align_tojEB5_.exit
  %1 = icmp eq i64 %aligned_address.i.i, %addr.i.i
  br i1 %1, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit, label %bb9.i.i.i.preheader

bb9.i.i.i.preheader:                              ; preds = %bb21
  %min.iters.check = icmp samesign ult i64 %byte_offset.i.i, 4
  br i1 %min.iters.check, label %bb9.i.i.i.preheader125, label %vector.ph

vector.ph:                                        ; preds = %bb9.i.i.i.preheader
  %n.vec = and i64 %byte_offset.i.i, 4
  br label %vector.body

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ]
  %vec.phi = phi <2 x i64> [ zeroinitializer, %vector.ph ], [ %8, %vector.body ]
  %vec.phi72 = phi <2 x i64> [ zeroinitializer, %vector.ph ], [ %9, %vector.body ]
  %2 = getelementptr inbounds nuw i8, ptr %s.0, i64 %index
  %3 = getelementptr inbounds nuw i8, ptr %2, i64 2
  %wide.load = load <2 x i8>, ptr %2, align 1, !alias.scope !34, !noalias !41
  %wide.load73 = load <2 x i8>, ptr %3, align 1, !alias.scope !34, !noalias !41
  %4 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %5 = icmp sgt <2 x i8> %wide.load73, splat (i8 -65)
  %6 = zext <2 x i1> %4 to <2 x i64>
  %7 = zext <2 x i1> %5 to <2 x i64>
  %8 = add <2 x i64> %vec.phi, %6
  %9 = add <2 x i64> %vec.phi72, %7
  %index.next = add nuw i64 %index, 4
  %10 = icmp eq i64 %index.next, %n.vec
  br i1 %10, label %middle.block, label %vector.body, !llvm.loop !44

middle.block:                                     ; preds = %vector.body
  %bin.rdx = add <2 x i64> %9, %8
  %11 = tail call i64 @llvm.vector.reduce.add.v2i64(<2 x i64> %bin.rdx)
  %cmp.n = icmp eq i64 %byte_offset.i.i, %n.vec
  br i1 %cmp.n, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit, label %bb9.i.i.i.preheader125

bb9.i.i.i.preheader125:                           ; preds = %middle.block, %bb9.i.i.i.preheader
  %i.sroa.0.0.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.preheader ], [ %n.vec, %middle.block ]
  %init.sroa.0.0.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.preheader ], [ %11, %middle.block ]
  br label %bb9.i.i.i

bb9.i.i.i:                                        ; preds = %bb9.i.i.i, %bb9.i.i.i.preheader125
  %i.sroa.0.0.i.i.i = phi i64 [ %_21.i.i.i, %bb9.i.i.i ], [ %i.sroa.0.0.i.i.i.ph, %bb9.i.i.i.preheader125 ]
  %init.sroa.0.0.i.i.i = phi i64 [ %_4.0.i.i.i.i.i, %bb9.i.i.i ], [ %init.sroa.0.0.i.i.i.ph, %bb9.i.i.i.preheader125 ]
  %_37.i.i.i = getelementptr inbounds nuw i8, ptr %s.0, i64 %i.sroa.0.0.i.i.i
  %byte.i.i.i.i.i.i = load i8, ptr %_37.i.i.i, align 1, !alias.scope !34, !noalias !41, !noundef !3
  %_4.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i, %_0.i.i.i.i.i
  %_21.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i, 1
  %_22.i.i.i = icmp eq i64 %_21.i.i.i, %byte_offset.i.i
  br i1 %_22.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit, label %bb9.i.i.i, !llvm.loop !45

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit: ; preds = %bb9.i.i.i, %middle.block, %bb21
  %_0.sroa.0.0.i.i.i = phi i64 [ 0, %bb21 ], [ %11, %middle.block ], [ %_4.0.i.i.i.i.i, %bb9.i.i.i ]
  %_3.i.i.i = icmp ule i64 %_0.sroa.0.0.i.i.i, %byte_offset.i.i
  tail call void @llvm.assume(i1 %_3.i.i.i)
  %12 = icmp samesign eq i64 %ts_len.i.i, 0
  br i1 %12, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17

bb9.i.i.i17:                                      ; preds = %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit
  %byte.i.i.i.i.i.i21 = load i8, ptr %_19.i, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22 = icmp sgt i8 %byte.i.i.i.i.i.i21, -65
  %_0.i.i.i.i.i23 = zext i1 %_4.i.i.i.i.i.i22 to i64
  %_22.i.i.i26 = icmp eq i64 %ts_len.i.i, 1
  br i1 %_22.i.i.i26, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.1

bb9.i.i.i17.1:                                    ; preds = %bb9.i.i.i17
  %_37.i.i.i20.1 = getelementptr inbounds nuw i8, ptr %_19.i, i64 1
  %byte.i.i.i.i.i.i21.1 = load i8, ptr %_37.i.i.i20.1, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.1 = icmp sgt i8 %byte.i.i.i.i.i.i21.1, -65
  %_0.i.i.i.i.i23.1 = zext i1 %_4.i.i.i.i.i.i22.1 to i64
  %_4.0.i.i.i.i.i24.1 = add nuw nsw i64 %_0.i.i.i.i.i23, %_0.i.i.i.i.i23.1
  %_22.i.i.i26.1 = icmp eq i64 %ts_len.i.i, 2
  br i1 %_22.i.i.i26.1, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.2

bb9.i.i.i17.2:                                    ; preds = %bb9.i.i.i17.1
  %_37.i.i.i20.2 = getelementptr inbounds nuw i8, ptr %_19.i, i64 2
  %byte.i.i.i.i.i.i21.2 = load i8, ptr %_37.i.i.i20.2, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.2 = icmp sgt i8 %byte.i.i.i.i.i.i21.2, -65
  %_0.i.i.i.i.i23.2 = zext i1 %_4.i.i.i.i.i.i22.2 to i64
  %_4.0.i.i.i.i.i24.2 = add nuw nsw i64 %_4.0.i.i.i.i.i24.1, %_0.i.i.i.i.i23.2
  %_22.i.i.i26.2 = icmp eq i64 %ts_len.i.i, 3
  br i1 %_22.i.i.i26.2, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.3

bb9.i.i.i17.3:                                    ; preds = %bb9.i.i.i17.2
  %_37.i.i.i20.3 = getelementptr inbounds nuw i8, ptr %_19.i, i64 3
  %byte.i.i.i.i.i.i21.3 = load i8, ptr %_37.i.i.i20.3, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.3 = icmp sgt i8 %byte.i.i.i.i.i.i21.3, -65
  %_0.i.i.i.i.i23.3 = zext i1 %_4.i.i.i.i.i.i22.3 to i64
  %_4.0.i.i.i.i.i24.3 = add nuw nsw i64 %_4.0.i.i.i.i.i24.2, %_0.i.i.i.i.i23.3
  %_22.i.i.i26.3 = icmp eq i64 %ts_len.i.i, 4
  br i1 %_22.i.i.i26.3, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.4

bb9.i.i.i17.4:                                    ; preds = %bb9.i.i.i17.3
  %_37.i.i.i20.4 = getelementptr inbounds nuw i8, ptr %_19.i, i64 4
  %byte.i.i.i.i.i.i21.4 = load i8, ptr %_37.i.i.i20.4, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.4 = icmp sgt i8 %byte.i.i.i.i.i.i21.4, -65
  %_0.i.i.i.i.i23.4 = zext i1 %_4.i.i.i.i.i.i22.4 to i64
  %_4.0.i.i.i.i.i24.4 = add nuw nsw i64 %_4.0.i.i.i.i.i24.3, %_0.i.i.i.i.i23.4
  %_22.i.i.i26.4 = icmp eq i64 %ts_len.i.i, 5
  br i1 %_22.i.i.i26.4, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.5

bb9.i.i.i17.5:                                    ; preds = %bb9.i.i.i17.4
  %_37.i.i.i20.5 = getelementptr inbounds nuw i8, ptr %_19.i, i64 5
  %byte.i.i.i.i.i.i21.5 = load i8, ptr %_37.i.i.i20.5, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.5 = icmp sgt i8 %byte.i.i.i.i.i.i21.5, -65
  %_0.i.i.i.i.i23.5 = zext i1 %_4.i.i.i.i.i.i22.5 to i64
  %_4.0.i.i.i.i.i24.5 = add nuw nsw i64 %_4.0.i.i.i.i.i24.4, %_0.i.i.i.i.i23.5
  %_22.i.i.i26.5 = icmp eq i64 %ts_len.i.i, 6
  br i1 %_22.i.i.i26.5, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.6

bb9.i.i.i17.6:                                    ; preds = %bb9.i.i.i17.5
  %_37.i.i.i20.6 = getelementptr inbounds nuw i8, ptr %_19.i, i64 6
  %byte.i.i.i.i.i.i21.6 = load i8, ptr %_37.i.i.i20.6, align 1, !alias.scope !46, !noalias !53, !noundef !3
  %_4.i.i.i.i.i.i22.6 = icmp sgt i8 %byte.i.i.i.i.i.i21.6, -65
  %_0.i.i.i.i.i23.6 = zext i1 %_4.i.i.i.i.i.i22.6 to i64
  %_4.0.i.i.i.i.i24.6 = add nuw nsw i64 %_4.0.i.i.i.i.i24.5, %_0.i.i.i.i.i23.6
  br label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29: ; preds = %bb9.i.i.i17.6, %bb9.i.i.i17.5, %bb9.i.i.i17.4, %bb9.i.i.i17.3, %bb9.i.i.i17.2, %bb9.i.i.i17.1, %bb9.i.i.i17, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit
  %_0.sroa.0.0.i.i.i27 = phi i64 [ 0, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit ], [ %_0.i.i.i.i.i23, %bb9.i.i.i17 ], [ %_4.0.i.i.i.i.i24.1, %bb9.i.i.i17.1 ], [ %_4.0.i.i.i.i.i24.2, %bb9.i.i.i17.2 ], [ %_4.0.i.i.i.i.i24.3, %bb9.i.i.i17.3 ], [ %_4.0.i.i.i.i.i24.4, %bb9.i.i.i17.4 ], [ %_4.0.i.i.i.i.i24.5, %bb9.i.i.i17.5 ], [ %_4.0.i.i.i.i.i24.6, %bb9.i.i.i17.6 ]
  %_3.i.i.i28 = icmp ule i64 %_0.sroa.0.0.i.i.i27, %ts_len.i.i
  tail call void @llvm.assume(i1 %_3.i.i.i28)
  %13 = add i64 %_0.sroa.0.0.i.i.i27, %_0.sroa.0.0.i.i.i
  br label %bb10

bb19:                                             ; preds = %_RINvMNtCscliFh4jUES5_4core5sliceSh8align_tojEB5_.exit, %start
  %14 = icmp samesign eq i64 %s.1, 0
  br i1 %14, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42, label %bb9.i.i.i30.preheader

bb9.i.i.i30.preheader:                            ; preds = %bb19
  %min.iters.check99 = icmp ult i64 %s.1, 4
  br i1 %min.iters.check99, label %bb9.i.i.i30.preheader115, label %vector.ph100

vector.ph100:                                     ; preds = %bb9.i.i.i30.preheader
  %n.vec102 = and i64 %s.1, -4
  br label %vector.body103

vector.body103:                                   ; preds = %vector.body103, %vector.ph100
  %index104 = phi i64 [ 0, %vector.ph100 ], [ %index.next109, %vector.body103 ]
  %vec.phi105 = phi <2 x i64> [ zeroinitializer, %vector.ph100 ], [ %21, %vector.body103 ]
  %vec.phi106 = phi <2 x i64> [ zeroinitializer, %vector.ph100 ], [ %22, %vector.body103 ]
  %15 = getelementptr inbounds nuw i8, ptr %s.0, i64 %index104
  %16 = getelementptr inbounds nuw i8, ptr %15, i64 2
  %wide.load107 = load <2 x i8>, ptr %15, align 1, !alias.scope !56, !noalias !63
  %wide.load108 = load <2 x i8>, ptr %16, align 1, !alias.scope !56, !noalias !63
  %17 = icmp sgt <2 x i8> %wide.load107, splat (i8 -65)
  %18 = icmp sgt <2 x i8> %wide.load108, splat (i8 -65)
  %19 = zext <2 x i1> %17 to <2 x i64>
  %20 = zext <2 x i1> %18 to <2 x i64>
  %21 = add <2 x i64> %vec.phi105, %19
  %22 = add <2 x i64> %vec.phi106, %20
  %index.next109 = add nuw i64 %index104, 4
  %23 = icmp eq i64 %index.next109, %n.vec102
  br i1 %23, label %middle.block110, label %vector.body103, !llvm.loop !66

middle.block110:                                  ; preds = %vector.body103
  %bin.rdx111 = add <2 x i64> %22, %21
  %24 = tail call i64 @llvm.vector.reduce.add.v2i64(<2 x i64> %bin.rdx111)
  %cmp.n112 = icmp eq i64 %s.1, %n.vec102
  br i1 %cmp.n112, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42, label %bb9.i.i.i30.preheader115

bb9.i.i.i30.preheader115:                         ; preds = %middle.block110, %bb9.i.i.i30.preheader
  %i.sroa.0.0.i.i.i31.ph = phi i64 [ 0, %bb9.i.i.i30.preheader ], [ %n.vec102, %middle.block110 ]
  %init.sroa.0.0.i.i.i32.ph = phi i64 [ 0, %bb9.i.i.i30.preheader ], [ %24, %middle.block110 ]
  br label %bb9.i.i.i30

bb9.i.i.i30:                                      ; preds = %bb9.i.i.i30, %bb9.i.i.i30.preheader115
  %i.sroa.0.0.i.i.i31 = phi i64 [ %_21.i.i.i38, %bb9.i.i.i30 ], [ %i.sroa.0.0.i.i.i31.ph, %bb9.i.i.i30.preheader115 ]
  %init.sroa.0.0.i.i.i32 = phi i64 [ %_4.0.i.i.i.i.i37, %bb9.i.i.i30 ], [ %init.sroa.0.0.i.i.i32.ph, %bb9.i.i.i30.preheader115 ]
  %_37.i.i.i33 = getelementptr inbounds nuw i8, ptr %s.0, i64 %i.sroa.0.0.i.i.i31
  %byte.i.i.i.i.i.i34 = load i8, ptr %_37.i.i.i33, align 1, !alias.scope !56, !noalias !63, !noundef !3
  %_4.i.i.i.i.i.i35 = icmp sgt i8 %byte.i.i.i.i.i.i34, -65
  %_0.i.i.i.i.i36 = zext i1 %_4.i.i.i.i.i.i35 to i64
  %_4.0.i.i.i.i.i37 = add i64 %init.sroa.0.0.i.i.i32, %_0.i.i.i.i.i36
  %_21.i.i.i38 = add nuw nsw i64 %i.sroa.0.0.i.i.i31, 1
  %_22.i.i.i39 = icmp eq i64 %_21.i.i.i38, %s.1
  br i1 %_22.i.i.i39, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42, label %bb9.i.i.i30, !llvm.loop !67

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42: ; preds = %bb9.i.i.i30, %middle.block110, %bb19
  %_0.sroa.0.0.i.i.i40 = phi i64 [ 0, %bb19 ], [ %24, %middle.block110 ], [ %_4.0.i.i.i.i.i37, %bb9.i.i.i30 ]
  %_3.i.i.i41 = icmp ule i64 %_0.sroa.0.0.i.i.i40, %s.1
  tail call void @llvm.assume(i1 %_3.i.i.i41)
  br label %bb18

bb10:                                             ; preds = %bb28, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29
  %body.sroa.0.0 = phi ptr [ %_13.i.i, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29 ], [ %_13.i, %bb28 ]
  %body.sroa.6.0 = phi i64 [ %_91.i.i, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29 ], [ %_14.i, %bb28 ]
  %total.sroa.0.0 = phi i64 [ %13, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29 ], [ %86, %bb28 ]
  %25 = icmp eq i64 %body.sroa.6.0, 0
  br i1 %25, label %bb18, label %bb23

bb23:                                             ; preds = %bb10
  %spec.store.select = tail call i64 @llvm.umin.i64(i64 %body.sroa.6.0, i64 192)
  %_13.i = getelementptr inbounds nuw i64, ptr %body.sroa.0.0, i64 %spec.store.select
  %_14.i = sub nuw nsw i64 %body.sroa.6.0, %spec.store.select
  %_24.i = and i64 %spec.store.select, 3
  %26 = shl nuw nsw i64 %spec.store.select, 3
  %_48.idx = and i64 %26, 2016
  %_48 = getelementptr inbounds nuw i8, ptr %body.sroa.0.0, i64 %_48.idx
  %_5453 = icmp samesign eq i64 %_48.idx, 0
  br i1 %_5453, label %bb28, label %bb29.preheader

bb29.preheader:                                   ; preds = %bb23
  %27 = add nsw i64 %26, -32
  %28 = lshr i64 %27, 5
  %29 = add nuw nsw i64 %28, 1
  %min.iters.check75 = icmp ult i64 %27, 96
  br i1 %min.iters.check75, label %bb29.preheader118, label %vector.ph76

vector.ph76:                                      ; preds = %bb29.preheader
  %n.vec78 = and i64 %29, 1152921504606846972
  %30 = shl i64 %n.vec78, 5
  %31 = getelementptr i8, ptr %body.sroa.0.0, i64 %30
  %invariant.gep = getelementptr i8, ptr %body.sroa.0.0, i64 64
  br label %vector.body79

vector.body79:                                    ; preds = %vector.body79, %vector.ph76
  %index80 = phi i64 [ 0, %vector.ph76 ], [ %index.next92, %vector.body79 ]
  %vec.phi81 = phi <2 x i64> [ zeroinitializer, %vector.ph76 ], [ %78, %vector.body79 ]
  %vec.phi82 = phi <2 x i64> [ zeroinitializer, %vector.ph76 ], [ %79, %vector.body79 ]
  %offset.idx = shl i64 %index80, 5
  %next.gep = getelementptr i8, ptr %body.sroa.0.0, i64 %offset.idx
  %gep = getelementptr i8, ptr %invariant.gep, i64 %offset.idx
  %wide.vec = load <8 x i64>, ptr %next.gep, align 8
  %strided.vec = shufflevector <8 x i64> %wide.vec, <8 x i64> poison, <2 x i32> <i32 0, i32 4>
  %strided.vec84 = shufflevector <8 x i64> %wide.vec, <8 x i64> poison, <2 x i32> <i32 1, i32 5>
  %strided.vec85 = shufflevector <8 x i64> %wide.vec, <8 x i64> poison, <2 x i32> <i32 2, i32 6>
  %strided.vec86 = shufflevector <8 x i64> %wide.vec, <8 x i64> poison, <2 x i32> <i32 3, i32 7>
  %wide.vec87 = load <8 x i64>, ptr %gep, align 8
  %strided.vec88 = shufflevector <8 x i64> %wide.vec87, <8 x i64> poison, <2 x i32> <i32 0, i32 4>
  %strided.vec89 = shufflevector <8 x i64> %wide.vec87, <8 x i64> poison, <2 x i32> <i32 1, i32 5>
  %strided.vec90 = shufflevector <8 x i64> %wide.vec87, <8 x i64> poison, <2 x i32> <i32 2, i32 6>
  %strided.vec91 = shufflevector <8 x i64> %wide.vec87, <8 x i64> poison, <2 x i32> <i32 3, i32 7>
  %32 = xor <2 x i64> %strided.vec, splat (i64 -1)
  %33 = xor <2 x i64> %strided.vec88, splat (i64 -1)
  %34 = lshr <2 x i64> %32, splat (i64 7)
  %35 = lshr <2 x i64> %33, splat (i64 7)
  %36 = lshr <2 x i64> %strided.vec, splat (i64 6)
  %37 = lshr <2 x i64> %strided.vec88, splat (i64 6)
  %38 = or <2 x i64> %34, %36
  %39 = or <2 x i64> %35, %37
  %40 = and <2 x i64> %38, splat (i64 72340172838076673)
  %41 = and <2 x i64> %39, splat (i64 72340172838076673)
  %42 = add <2 x i64> %40, %vec.phi81
  %43 = add <2 x i64> %41, %vec.phi82
  %44 = xor <2 x i64> %strided.vec84, splat (i64 -1)
  %45 = xor <2 x i64> %strided.vec89, splat (i64 -1)
  %46 = lshr <2 x i64> %44, splat (i64 7)
  %47 = lshr <2 x i64> %45, splat (i64 7)
  %48 = lshr <2 x i64> %strided.vec84, splat (i64 6)
  %49 = lshr <2 x i64> %strided.vec89, splat (i64 6)
  %50 = or <2 x i64> %46, %48
  %51 = or <2 x i64> %47, %49
  %52 = and <2 x i64> %50, splat (i64 72340172838076673)
  %53 = and <2 x i64> %51, splat (i64 72340172838076673)
  %54 = add <2 x i64> %52, %42
  %55 = add <2 x i64> %53, %43
  %56 = xor <2 x i64> %strided.vec85, splat (i64 -1)
  %57 = xor <2 x i64> %strided.vec90, splat (i64 -1)
  %58 = lshr <2 x i64> %56, splat (i64 7)
  %59 = lshr <2 x i64> %57, splat (i64 7)
  %60 = lshr <2 x i64> %strided.vec85, splat (i64 6)
  %61 = lshr <2 x i64> %strided.vec90, splat (i64 6)
  %62 = or <2 x i64> %58, %60
  %63 = or <2 x i64> %59, %61
  %64 = and <2 x i64> %62, splat (i64 72340172838076673)
  %65 = and <2 x i64> %63, splat (i64 72340172838076673)
  %66 = add <2 x i64> %64, %54
  %67 = add <2 x i64> %65, %55
  %68 = xor <2 x i64> %strided.vec86, splat (i64 -1)
  %69 = xor <2 x i64> %strided.vec91, splat (i64 -1)
  %70 = lshr <2 x i64> %68, splat (i64 7)
  %71 = lshr <2 x i64> %69, splat (i64 7)
  %72 = lshr <2 x i64> %strided.vec86, splat (i64 6)
  %73 = lshr <2 x i64> %strided.vec91, splat (i64 6)
  %74 = or <2 x i64> %70, %72
  %75 = or <2 x i64> %71, %73
  %76 = and <2 x i64> %74, splat (i64 72340172838076673)
  %77 = and <2 x i64> %75, splat (i64 72340172838076673)
  %78 = add <2 x i64> %76, %66
  %79 = add <2 x i64> %77, %67
  %index.next92 = add nuw i64 %index80, 4
  %80 = icmp eq i64 %index.next92, %n.vec78
  br i1 %80, label %middle.block93, label %vector.body79, !llvm.loop !68

middle.block93:                                   ; preds = %vector.body79
  %bin.rdx94 = add <2 x i64> %79, %78
  %81 = tail call i64 @llvm.vector.reduce.add.v2i64(<2 x i64> %bin.rdx94)
  %cmp.n95 = icmp eq i64 %29, %n.vec78
  br i1 %cmp.n95, label %bb28, label %bb29.preheader118

bb29.preheader118:                                ; preds = %middle.block93, %bb29.preheader
  %counts.sroa.0.055.ph = phi i64 [ 0, %bb29.preheader ], [ %81, %middle.block93 ]
  %iter.sroa.0.054.ph = phi ptr [ %body.sroa.0.0, %bb29.preheader ], [ %31, %middle.block93 ]
  br label %bb29

bb29:                                             ; preds = %bb29, %bb29.preheader118
  %counts.sroa.0.055 = phi i64 [ %85, %bb29 ], [ %counts.sroa.0.055.ph, %bb29.preheader118 ]
  %iter.sroa.0.054 = phi ptr [ %_60, %bb29 ], [ %iter.sroa.0.054.ph, %bb29.preheader118 ]
  %word = load i64, ptr %iter.sroa.0.054, align 8, !noundef !3
  %_86 = xor i64 %word, -1
  %_85 = lshr i64 %_86, 7
  %_87 = lshr i64 %word, 6
  %_84 = or i64 %_85, %_87
  %_24 = and i64 %_84, 72340172838076673
  %82 = add i64 %_24, %counts.sroa.0.055
  %iter1.sroa.0.0.ptr.1 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 8
  %word.1 = load i64, ptr %iter1.sroa.0.0.ptr.1, align 8, !noundef !3
  %_86.1 = xor i64 %word.1, -1
  %_85.1 = lshr i64 %_86.1, 7
  %_87.1 = lshr i64 %word.1, 6
  %_84.1 = or i64 %_85.1, %_87.1
  %_24.1 = and i64 %_84.1, 72340172838076673
  %83 = add i64 %_24.1, %82
  %iter1.sroa.0.0.ptr.2 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 16
  %word.2 = load i64, ptr %iter1.sroa.0.0.ptr.2, align 8, !noundef !3
  %_86.2 = xor i64 %word.2, -1
  %_85.2 = lshr i64 %_86.2, 7
  %_87.2 = lshr i64 %word.2, 6
  %_84.2 = or i64 %_85.2, %_87.2
  %_24.2 = and i64 %_84.2, 72340172838076673
  %84 = add i64 %_24.2, %83
  %iter1.sroa.0.0.ptr.3 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 24
  %word.3 = load i64, ptr %iter1.sroa.0.0.ptr.3, align 8, !noundef !3
  %_86.3 = xor i64 %word.3, -1
  %_85.3 = lshr i64 %_86.3, 7
  %_87.3 = lshr i64 %word.3, 6
  %_84.3 = or i64 %_85.3, %_87.3
  %_24.3 = and i64 %_84.3, 72340172838076673
  %85 = add i64 %_24.3, %84
  %_60 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 32
  %_54 = icmp eq ptr %_60, %_48
  br i1 %_54, label %bb28, label %bb29, !llvm.loop !69

bb28:                                             ; preds = %bb29, %middle.block93, %bb23
  %counts.sroa.0.0.lcssa = phi i64 [ 0, %bb23 ], [ %81, %middle.block93 ], [ %85, %bb29 ]
  %_70 = and i64 %counts.sroa.0.0.lcssa, 71777214294589695
  %_72 = lshr i64 %counts.sroa.0.0.lcssa, 8
  %_71 = and i64 %_72, 71777214294589695
  %_69 = add nuw nsw i64 %_71, %_70
  %_73 = mul i64 %_69, 281479271743489
  %_25 = lshr i64 %_73, 48
  %86 = add i64 %_25, %total.sroa.0.0
  %87 = icmp eq i64 %_24.i, 0
  br i1 %87, label %bb10, label %bb33.preheader

bb33.preheader:                                   ; preds = %bb28
  %len_rounded_down.i.le = and i64 %spec.store.select, 252
  %_23.i.le = getelementptr inbounds nuw i64, ptr %body.sroa.0.0, i64 %len_rounded_down.i.le
  %word4 = load i64, ptr %_23.i.le, align 8, !noundef !3
  %_107 = xor i64 %word4, -1
  %_106 = lshr i64 %_107, 7
  %_108 = lshr i64 %word4, 6
  %_105 = or i64 %_106, %_108
  %_30 = and i64 %_105, 72340172838076673
  %_97 = icmp eq i64 %_24.i, 1
  br i1 %_97, label %bb32, label %bb33.1

bb33.1:                                           ; preds = %bb33.preheader
  %_103 = getelementptr inbounds nuw i8, ptr %_23.i.le, i64 8
  %word4.1 = load i64, ptr %_103, align 8, !noundef !3
  %_107.1 = xor i64 %word4.1, -1
  %_106.1 = lshr i64 %_107.1, 7
  %_108.1 = lshr i64 %word4.1, 6
  %_105.1 = or i64 %_106.1, %_108.1
  %_30.1 = and i64 %_105.1, 72340172838076673
  %88 = add nuw nsw i64 %_30.1, %_30
  %_97.1 = icmp eq i64 %_24.i, 2
  br i1 %_97.1, label %bb32, label %bb33.2

bb33.2:                                           ; preds = %bb33.1
  %_103.1 = getelementptr inbounds nuw i8, ptr %_23.i.le, i64 16
  %word4.2 = load i64, ptr %_103.1, align 8, !noundef !3
  %_107.2 = xor i64 %word4.2, -1
  %_106.2 = lshr i64 %_107.2, 7
  %_108.2 = lshr i64 %word4.2, 6
  %_105.2 = or i64 %_106.2, %_108.2
  %_30.2 = and i64 %_105.2, 72340172838076673
  %89 = add nuw nsw i64 %_30.2, %88
  br label %bb32

bb32:                                             ; preds = %bb33.2, %bb33.1, %bb33.preheader
  %.lcssa117 = phi i64 [ %_30, %bb33.preheader ], [ %88, %bb33.1 ], [ %89, %bb33.2 ]
  %_110 = and i64 %.lcssa117, 71777214294589695
  %_112 = lshr i64 %.lcssa117, 8
  %_111 = and i64 %_112, 71777214294589695
  %_109 = add nuw nsw i64 %_111, %_110
  %_113 = mul i64 %_109, 281479271743489
  %_31 = lshr i64 %_113, 48
  %90 = add i64 %_31, %86
  br label %bb18

bb18:                                             ; preds = %bb32, %bb10, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42
  %total.sroa.0.2 = phi i64 [ %_0.sroa.0.0.i.i.i40, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42 ], [ %90, %bb32 ], [ %total.sroa.0.0, %bb10 ]
  ret i64 %total.sroa.0.2
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.vector.reduce.add.v2i64(<2 x i64>) #5

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter12pad_integral(ptr noalias noundef align 8 captures(none) dereferenceable(24) %self, i1 noundef zeroext %is_nonnegative, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %prefix.0, i64 noundef %prefix.1, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) unnamed_addr #1 {
start:
  br i1 %is_nonnegative, label %bb2, label %bb1

bb1:                                              ; preds = %start
  %0 = add i64 %buf.1, 1
  %.phi.trans.insert = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_38.pre = load i32, ptr %.phi.trans.insert, align 8
  br label %bb6

bb2:                                              ; preds = %start
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_36 = load i32, ptr %1, align 8, !noundef !3
  %_35 = and i32 %_36, 2097152
  %.not = icmp eq i32 %_35, 0
  %spec.select = select i1 %.not, i32 1114112, i32 43
  %_35.lobit = lshr exact i32 %_35, 21
  %2 = zext nneg i32 %_35.lobit to i64
  %spec.select19 = add i64 %buf.1, %2
  br label %bb6

bb6:                                              ; preds = %bb2, %bb1
  %_38 = phi i32 [ %_38.pre, %bb1 ], [ %_36, %bb2 ]
  %sign.sroa.0.0 = phi i32 [ 45, %bb1 ], [ %spec.select, %bb2 ]
  %width.sroa.0.0 = phi i64 [ %0, %bb1 ], [ %spec.select19, %bb2 ]
  %3 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_37 = and i32 %_38, 8388608
  %4 = icmp eq i32 %_37, 0
  br i1 %4, label %bb10, label %bb7

bb7:                                              ; preds = %bb6
  %_16.i = icmp ult i64 %prefix.1, 32
  br i1 %_16.i, label %bb8.i, label %bb9.i

bb9.i:                                            ; preds = %bb7
  %5 = tail call noundef i64 @_RNvNtNtCscliFh4jUES5_4core3str5count14do_count_chars(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %prefix.0, i64 noundef %prefix.1) #17
  br label %_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit

bb8.i:                                            ; preds = %bb7
  %6 = icmp samesign eq i64 %prefix.1, 0
  br i1 %6, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i.preheader

bb9.i.i.i.i.preheader:                            ; preds = %bb8.i
  %min.iters.check = icmp ult i64 %prefix.1, 4
  br i1 %min.iters.check, label %bb9.i.i.i.i.preheader70, label %vector.ph

vector.ph:                                        ; preds = %bb9.i.i.i.i.preheader
  %n.vec = and i64 %prefix.1, 28
  %7 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 2
  %wide.load = load <2 x i8>, ptr %prefix.0, align 1, !alias.scope !70, !noalias !77
  %wide.load68 = load <2 x i8>, ptr %7, align 1, !alias.scope !70, !noalias !77
  %8 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %9 = icmp sgt <2 x i8> %wide.load68, splat (i8 -65)
  %10 = zext <2 x i1> %8 to <2 x i64>
  %11 = zext <2 x i1> %9 to <2 x i64>
  %12 = icmp eq i64 %n.vec, 4
  br i1 %12, label %middle.block, label %vector.body.1, !llvm.loop !80

vector.body.1:                                    ; preds = %vector.ph
  %13 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 4
  %14 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 6
  %wide.load.1 = load <2 x i8>, ptr %13, align 1, !alias.scope !70, !noalias !77
  %wide.load68.1 = load <2 x i8>, ptr %14, align 1, !alias.scope !70, !noalias !77
  %15 = icmp sgt <2 x i8> %wide.load.1, splat (i8 -65)
  %16 = icmp sgt <2 x i8> %wide.load68.1, splat (i8 -65)
  %17 = zext <2 x i1> %15 to <2 x i64>
  %18 = zext <2 x i1> %16 to <2 x i64>
  %19 = add nuw nsw <2 x i64> %10, %17
  %20 = add nuw nsw <2 x i64> %11, %18
  %21 = icmp eq i64 %n.vec, 8
  br i1 %21, label %middle.block, label %vector.body.2, !llvm.loop !80

vector.body.2:                                    ; preds = %vector.body.1
  %22 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 8
  %23 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 10
  %wide.load.2 = load <2 x i8>, ptr %22, align 1, !alias.scope !70, !noalias !77
  %wide.load68.2 = load <2 x i8>, ptr %23, align 1, !alias.scope !70, !noalias !77
  %24 = icmp sgt <2 x i8> %wide.load.2, splat (i8 -65)
  %25 = icmp sgt <2 x i8> %wide.load68.2, splat (i8 -65)
  %26 = zext <2 x i1> %24 to <2 x i64>
  %27 = zext <2 x i1> %25 to <2 x i64>
  %28 = add nuw nsw <2 x i64> %19, %26
  %29 = add nuw nsw <2 x i64> %20, %27
  %30 = icmp eq i64 %n.vec, 12
  br i1 %30, label %middle.block, label %vector.body.3, !llvm.loop !80

vector.body.3:                                    ; preds = %vector.body.2
  %31 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 12
  %32 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 14
  %wide.load.3 = load <2 x i8>, ptr %31, align 1, !alias.scope !70, !noalias !77
  %wide.load68.3 = load <2 x i8>, ptr %32, align 1, !alias.scope !70, !noalias !77
  %33 = icmp sgt <2 x i8> %wide.load.3, splat (i8 -65)
  %34 = icmp sgt <2 x i8> %wide.load68.3, splat (i8 -65)
  %35 = zext <2 x i1> %33 to <2 x i64>
  %36 = zext <2 x i1> %34 to <2 x i64>
  %37 = add nuw nsw <2 x i64> %28, %35
  %38 = add nuw nsw <2 x i64> %29, %36
  %39 = icmp eq i64 %n.vec, 16
  br i1 %39, label %middle.block, label %vector.body.4, !llvm.loop !80

vector.body.4:                                    ; preds = %vector.body.3
  %40 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 16
  %41 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 18
  %wide.load.4 = load <2 x i8>, ptr %40, align 1, !alias.scope !70, !noalias !77
  %wide.load68.4 = load <2 x i8>, ptr %41, align 1, !alias.scope !70, !noalias !77
  %42 = icmp sgt <2 x i8> %wide.load.4, splat (i8 -65)
  %43 = icmp sgt <2 x i8> %wide.load68.4, splat (i8 -65)
  %44 = zext <2 x i1> %42 to <2 x i64>
  %45 = zext <2 x i1> %43 to <2 x i64>
  %46 = add nuw nsw <2 x i64> %37, %44
  %47 = add nuw nsw <2 x i64> %38, %45
  %48 = icmp eq i64 %n.vec, 20
  br i1 %48, label %middle.block, label %vector.body.5, !llvm.loop !80

vector.body.5:                                    ; preds = %vector.body.4
  %49 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 20
  %50 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 22
  %wide.load.5 = load <2 x i8>, ptr %49, align 1, !alias.scope !70, !noalias !77
  %wide.load68.5 = load <2 x i8>, ptr %50, align 1, !alias.scope !70, !noalias !77
  %51 = icmp sgt <2 x i8> %wide.load.5, splat (i8 -65)
  %52 = icmp sgt <2 x i8> %wide.load68.5, splat (i8 -65)
  %53 = zext <2 x i1> %51 to <2 x i64>
  %54 = zext <2 x i1> %52 to <2 x i64>
  %55 = add nuw nsw <2 x i64> %46, %53
  %56 = add nuw nsw <2 x i64> %47, %54
  %57 = icmp eq i64 %n.vec, 24
  br i1 %57, label %middle.block, label %vector.body.6, !llvm.loop !80

vector.body.6:                                    ; preds = %vector.body.5
  %58 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 24
  %59 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 26
  %wide.load.6 = load <2 x i8>, ptr %58, align 1, !alias.scope !70, !noalias !77
  %wide.load68.6 = load <2 x i8>, ptr %59, align 1, !alias.scope !70, !noalias !77
  %60 = icmp sgt <2 x i8> %wide.load.6, splat (i8 -65)
  %61 = icmp sgt <2 x i8> %wide.load68.6, splat (i8 -65)
  %62 = zext <2 x i1> %60 to <2 x i64>
  %63 = zext <2 x i1> %61 to <2 x i64>
  %64 = add nuw nsw <2 x i64> %55, %62
  %65 = add nuw nsw <2 x i64> %56, %63
  br label %middle.block

middle.block:                                     ; preds = %vector.body.6, %vector.body.5, %vector.body.4, %vector.body.3, %vector.body.2, %vector.body.1, %vector.ph
  %.lcssa71 = phi <2 x i64> [ %10, %vector.ph ], [ %19, %vector.body.1 ], [ %28, %vector.body.2 ], [ %37, %vector.body.3 ], [ %46, %vector.body.4 ], [ %55, %vector.body.5 ], [ %64, %vector.body.6 ]
  %.lcssa = phi <2 x i64> [ %11, %vector.ph ], [ %20, %vector.body.1 ], [ %29, %vector.body.2 ], [ %38, %vector.body.3 ], [ %47, %vector.body.4 ], [ %56, %vector.body.5 ], [ %65, %vector.body.6 ]
  %bin.rdx = add <2 x i64> %.lcssa, %.lcssa71
  %66 = tail call i64 @llvm.vector.reduce.add.v2i64(<2 x i64> %bin.rdx)
  %cmp.n = icmp eq i64 %prefix.1, %n.vec
  br i1 %cmp.n, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i.preheader70

bb9.i.i.i.i.preheader70:                          ; preds = %middle.block, %bb9.i.i.i.i.preheader
  %i.sroa.0.0.i.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.i.preheader ], [ %n.vec, %middle.block ]
  %init.sroa.0.0.i.i.i.i.ph = phi i64 [ 0, %bb9.i.i.i.i.preheader ], [ %66, %middle.block ]
  br label %bb9.i.i.i.i

bb9.i.i.i.i:                                      ; preds = %bb9.i.i.i.i, %bb9.i.i.i.i.preheader70
  %i.sroa.0.0.i.i.i.i = phi i64 [ %_21.i.i.i.i, %bb9.i.i.i.i ], [ %i.sroa.0.0.i.i.i.i.ph, %bb9.i.i.i.i.preheader70 ]
  %init.sroa.0.0.i.i.i.i = phi i64 [ %_4.0.i.i.i.i.i.i, %bb9.i.i.i.i ], [ %init.sroa.0.0.i.i.i.i.ph, %bb9.i.i.i.i.preheader70 ]
  %_37.i.i.i.i = getelementptr inbounds nuw i8, ptr %prefix.0, i64 %i.sroa.0.0.i.i.i.i
  %byte.i.i.i.i.i.i.i = load i8, ptr %_37.i.i.i.i, align 1, !alias.scope !70, !noalias !77, !noundef !3
  %_4.i.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i.i, %_0.i.i.i.i.i.i
  %_21.i.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i.i, 1
  %_22.i.i.i.i = icmp eq i64 %_21.i.i.i.i, %prefix.1
  br i1 %_22.i.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i, !llvm.loop !81

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i: ; preds = %bb9.i.i.i.i, %middle.block, %bb8.i
  %_0.sroa.0.0.i.i.i.i = phi i64 [ 0, %bb8.i ], [ %66, %middle.block ], [ %_4.0.i.i.i.i.i.i, %bb9.i.i.i.i ]
  %_3.i.i.i.i = icmp ule i64 %_0.sroa.0.0.i.i.i.i, %prefix.1
  tail call void @llvm.assume(i1 %_3.i.i.i.i)
  br label %_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit

_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit: ; preds = %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, %bb9.i
  %_0.sroa.0.0.i = phi i64 [ %_0.sroa.0.0.i.i.i.i, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i ], [ %5, %bb9.i ]
  %67 = add i64 %_0.sroa.0.0.i, %width.sroa.0.0
  br label %bb10

bb10:                                             ; preds = %_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit, %bb6
  %prefix.sroa.0.0 = phi ptr [ %prefix.0, %_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit ], [ null, %bb6 ]
  %width.sroa.0.1 = phi i64 [ %67, %_RNvXNtNtCscliFh4jUES5_4core3str4iterNtB2_5CharsNtNtNtNtB6_4iter6traits8iterator8Iterator5count.exit ], [ %width.sroa.0.0, %bb6 ]
  %68 = getelementptr inbounds nuw i8, ptr %self, i64 20
  %min = load i16, ptr %68, align 4, !noundef !3
  %_13 = zext i16 %min to i64
  %_11.not = icmp ult i64 %width.sroa.0.1, %_13
  br i1 %_11.not, label %bb14, label %bb11

bb14:                                             ; preds = %bb10
  %_49 = and i32 %_38, 16777216
  %69 = icmp eq i32 %_49, 0
  br i1 %69, label %bb20, label %bb15

bb11:                                             ; preds = %bb10
  %_14 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #23
  br i1 %_14, label %bb31, label %bb33

bb20:                                             ; preds = %bb14
  %_27 = trunc nuw i64 %width.sroa.0.1 to i16
  %_26 = sub i16 %min, %_27
  tail call void @llvm.experimental.noalias.scope.decl(metadata !82)
  %_14.i = lshr i32 %_38, 29
  %70 = and i32 %_14.i, 3
  %_2.i11.i = and i32 %_38, 2097151
  %71 = icmp samesign ult i32 %_2.i11.i, 1114112
  tail call void @llvm.assume(i1 %71)
  switch i32 %70, label %default.unreachable [
    i32 0, label %bb6.i
    i32 1, label %bb12.i
    i32 2, label %bb11.i
    i32 3, label %bb12.i
  ]

default.unreachable:                              ; preds = %bb20
  unreachable

bb12.i:                                           ; preds = %bb20, %bb20
  br label %bb6.i

bb11.i:                                           ; preds = %bb20
  %72 = lshr i16 %_26, 1
  br label %bb6.i

bb6.i:                                            ; preds = %bb11.i, %bb12.i, %bb20
  %padding_left.sroa.0.0.i = phi i16 [ %72, %bb11.i ], [ %_26, %bb12.i ], [ 0, %bb20 ]
  %_13.0.i = load ptr, ptr %self, align 8, !alias.scope !82, !nonnull !3, !align !4
  %73 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i = load ptr, ptr %73, align 8, !alias.scope !82, !nonnull !3, !align !5
  %74 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 32
  br label %bb7.i

bb7.i:                                            ; preds = %bb17.i, %bb6.i
  %iter.sroa.0.0.i = phi i16 [ 0, %bb6.i ], [ %_17.i, %bb17.i ]
  %exitcond.not.i = icmp eq i16 %iter.sroa.0.0.i, %padding_left.sroa.0.0.i
  br i1 %exitcond.not.i, label %bb43, label %bb17.i

bb17.i:                                           ; preds = %bb7.i
  %_17.i = add i16 %iter.sroa.0.0.i, 1
  %75 = load ptr, ptr %74, align 8, !invariant.load !3, !noalias !82, !nonnull !3
  %_10.i = tail call noundef zeroext i1 %75(ptr noundef nonnull align 1 %_13.0.i, i32 noundef %_2.i11.i) #22, !noalias !82
  br i1 %_10.i, label %bb31, label %bb7.i

bb15:                                             ; preds = %bb14
  %old_options.sroa.0.0.copyload = load i64, ptr %3, align 8
  %76 = trunc i64 %old_options.sroa.0.0.copyload to i32
  %_51 = and i32 %76, -1612709888
  %77 = or disjoint i32 %_51, 536870960
  store i32 %77, ptr %3, align 8
  %_16 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #23
  br i1 %_16, label %bb31, label %bb35

bb43:                                             ; preds = %bb7.i
  %_12.i = sub i16 %_26, %padding_left.sroa.0.0.i
  %_29 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #23
  br i1 %_29, label %bb31, label %bb45

bb45:                                             ; preds = %bb43
  %78 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 24
  %79 = load ptr, ptr %78, align 8, !invariant.load !3, !nonnull !3
  %_30 = tail call noundef zeroext i1 %79(ptr noundef nonnull align 1 %_13.0.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #22
  br i1 %_30, label %bb31, label %bb1.i

bb1.i:                                            ; preds = %bb8.i24, %bb45
  %iter.sroa.0.0.i22 = phi i16 [ %_8.i, %bb8.i24 ], [ 0, %bb45 ]
  %exitcond.not.i23 = icmp eq i16 %iter.sroa.0.0.i22, %_12.i
  br i1 %exitcond.not.i23, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb8.i24

bb8.i24:                                          ; preds = %bb1.i
  %_8.i = add i16 %iter.sroa.0.0.i22, 1
  %80 = load ptr, ptr %74, align 8, !invariant.load !3, !noalias !85, !nonnull !3
  %_4.i = tail call noundef zeroext i1 %80(ptr noundef nonnull align 1 %_13.0.i, i32 noundef range(i32 0, 1114112) %_2.i11.i) #22, !noalias !85
  br i1 %_4.i, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb1.i

_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit: ; preds = %bb8.i24, %bb1.i
  %iter.sroa.0.0.i22.lcssa = phi i16 [ %_12.i, %bb1.i ], [ %iter.sroa.0.0.i22, %bb8.i24 ]
  %_7.i = icmp ult i16 %iter.sroa.0.0.i22.lcssa, %_12.i
  br label %bb31

bb35:                                             ; preds = %bb15
  %_20 = trunc nuw i64 %width.sroa.0.1 to i16
  %_19 = sub i16 %min, %_20
  tail call void @llvm.experimental.noalias.scope.decl(metadata !88)
  %_13.0.i31 = load ptr, ptr %self, align 8, !alias.scope !88, !nonnull !3, !align !4
  %81 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i32 = load ptr, ptr %81, align 8, !alias.scope !88, !nonnull !3, !align !5
  %82 = getelementptr inbounds nuw i8, ptr %_13.1.i32, i64 32
  br label %bb7.i33

bb7.i33:                                          ; preds = %bb17.i36, %bb35
  %iter.sroa.0.0.i34 = phi i16 [ 0, %bb35 ], [ %_17.i37, %bb17.i36 ]
  %exitcond.not.i35 = icmp eq i16 %iter.sroa.0.0.i34, %_19
  br i1 %exitcond.not.i35, label %bb37, label %bb17.i36

bb17.i36:                                         ; preds = %bb7.i33
  %_17.i37 = add i16 %iter.sroa.0.0.i34, 1
  %83 = load ptr, ptr %82, align 8, !invariant.load !3, !noalias !88, !nonnull !3
  %_10.i38 = tail call noundef zeroext i1 %83(ptr noundef nonnull align 1 %_13.0.i31, i32 noundef 48) #22, !noalias !88
  br i1 %_10.i38, label %bb31, label %bb7.i33

bb37:                                             ; preds = %bb7.i33
  %84 = getelementptr inbounds nuw i8, ptr %_13.1.i32, i64 24
  %85 = load ptr, ptr %84, align 8, !invariant.load !3, !nonnull !3
  %_22 = tail call noundef zeroext i1 %85(ptr noundef nonnull align 1 %_13.0.i31, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #22
  br i1 %_22, label %bb31, label %bb41

bb41:                                             ; preds = %bb37
  store i64 %old_options.sroa.0.0.copyload, ptr %3, align 8
  br label %bb31

bb33:                                             ; preds = %bb11
  %_31.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %86 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_31.1 = load ptr, ptr %86, align 8, !nonnull !3, !align !5, !noundef !3
  %87 = getelementptr inbounds nuw i8, ptr %_31.1, i64 24
  %88 = load ptr, ptr %87, align 8, !invariant.load !3, !nonnull !3
  %89 = tail call noundef zeroext i1 %88(ptr noundef nonnull align 1 %_31.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #22
  br label %bb31

bb31:                                             ; preds = %bb33, %bb41, %bb37, %bb17.i36, %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, %bb45, %bb43, %bb15, %bb17.i, %bb11
  %_0.sroa.0.0.shrunk = phi i1 [ %89, %bb33 ], [ %_7.i, %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit ], [ false, %bb41 ], [ true, %bb43 ], [ true, %bb45 ], [ true, %bb15 ], [ true, %bb37 ], [ true, %bb11 ], [ true, %bb17.i ], [ true, %bb17.i36 ]
  ret i1 %_0.sroa.0.0.shrunk
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc noundef range(i64 0, -9223372036854775808) i64 @_RNvMsf_NtNtNtCscliFh4jUES5_4core3fmt3num3impy10__fmt_inner(i64 noundef %self, ptr noalias noundef nonnull writeonly align 1 captures(none) %buf.0, i64 noundef range(i64 0, -9223372036854775808) %buf.1) unnamed_addr #1 {
start:
  %_461 = icmp ugt i64 %self, 999
  br i1 %_461, label %bb2, label %bb13

bb13:                                             ; preds = %bb12, %start
  %remain.sroa.0.0.lcssa = phi i64 [ %self, %start ], [ %1, %bb12 ]
  %offset.sroa.0.0.lcssa = phi i64 [ %buf.1, %start ], [ %0, %bb12 ]
  %_50 = icmp samesign ugt i64 %remain.sroa.0.0.lcssa, 9
  br i1 %_50, label %bb14, label %bb20

bb2:                                              ; preds = %bb12, %start
  %offset.sroa.0.063 = phi i64 [ %0, %bb12 ], [ %buf.1, %start ]
  %remain.sroa.0.062 = phi i64 [ %1, %bb12 ], [ %self, %start ]
  %_8 = icmp ugt i64 %offset.sroa.0.063, 3
  tail call void @llvm.assume(i1 %_8)
  %0 = add nsw i64 %offset.sroa.0.063, -4
  %quad = urem i64 %remain.sroa.0.062, 10000
  %1 = udiv i64 %remain.sroa.0.062, 10000
  %_21.lhs.trunc = trunc nuw nsw i64 %quad to i16
  %_2144 = urem i16 %_21.lhs.trunc, 100
  %_1945 = udiv i16 %_21.lhs.trunc, 100
  %_24 = icmp ult i64 %0, %buf.1
  br i1 %_24, label %bb5, label %panic8

bb14:                                             ; preds = %bb13
  %_52 = icmp ugt i64 %offset.sroa.0.0.lcssa, 1
  tail call void @llvm.assume(i1 %_52)
  %_54 = icmp ule i64 %offset.sroa.0.0.lcssa, %buf.1
  tail call void @llvm.assume(i1 %_54)
  %2 = add nsw i64 %offset.sroa.0.0.lcssa, -2
  %.lhs.trunc = trunc nuw i64 %remain.sroa.0.0.lcssa to i16
  %3 = udiv i16 %.lhs.trunc, 100
  %_5846 = urem i16 %.lhs.trunc, 100
  %.zext = zext nneg i16 %3 to i64
  %_62 = icmp ult i64 %2, %buf.1
  br i1 %_62, label %bb16, label %panic

bb20:                                             ; preds = %bb19, %bb13
  %remain.sroa.0.1 = phi i64 [ %.zext, %bb19 ], [ %remain.sroa.0.0.lcssa, %bb13 ]
  %offset.sroa.0.1 = phi i64 [ %2, %bb19 ], [ %offset.sroa.0.0.lcssa, %bb13 ]
  %4 = icmp ne i64 %remain.sroa.0.1, 0
  %5 = icmp eq i64 %self, 0
  %or.cond = or i1 %5, %4
  br i1 %or.cond, label %bb23, label %bb26

bb16:                                             ; preds = %bb14
  %6 = shl nuw nsw i16 %_5846, 1
  %_65 = zext nneg i16 %6 to i64
  %_60 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %2
  %7 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_65
  %_63 = load i8, ptr %7, align 1, !noundef !3
  store i8 %_63, ptr %_60, align 1
  %_68 = add nsw i64 %offset.sroa.0.0.lcssa, -1
  %_70 = icmp ult i64 %_68, %buf.1
  br i1 %_70, label %bb19, label %panic2

panic:                                            ; preds = %bb14
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %2, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

panic2:                                           ; preds = %bb16
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_68, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

bb19:                                             ; preds = %bb16
  %_67 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_68
  %8 = getelementptr inbounds nuw i8, ptr %7, i64 1
  %_71 = load i8, ptr %8, align 1, !noundef !3
  store i8 %_71, ptr %_67, align 1
  br label %bb20

bb23:                                             ; preds = %bb20
  %_75 = icmp ne i64 %offset.sroa.0.1, 0
  tail call void @llvm.assume(i1 %_75)
  %_77 = icmp ule i64 %offset.sroa.0.1, %buf.1
  tail call void @llvm.assume(i1 %_77)
  %9 = add nsw i64 %offset.sroa.0.1, -1
  %_84 = icmp ult i64 %9, %buf.1
  br i1 %_84, label %bb25, label %panic4

bb26:                                             ; preds = %bb25, %bb20
  %offset.sroa.0.2 = phi i64 [ %9, %bb25 ], [ %offset.sroa.0.1, %bb20 ]
  ret i64 %offset.sroa.0.2

panic4:                                           ; preds = %bb23
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %9, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

bb25:                                             ; preds = %bb23
  %_88 = shl nuw nsw i64 %remain.sroa.0.1, 1
  %_82 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %9
  %10 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_88
  %11 = getelementptr inbounds nuw i8, ptr %10, i64 1
  %_85 = load i8, ptr %11, align 1, !noundef !3
  store i8 %_85, ptr %_82, align 1
  br label %bb26

bb5:                                              ; preds = %bb2
  %12 = shl nuw nsw i16 %_1945, 1
  %_27 = zext nneg i16 %12 to i64
  %_22 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %0
  %13 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_27
  %_25 = load i8, ptr %13, align 1, !noundef !3
  store i8 %_25, ptr %_22, align 1
  %_30 = add nsw i64 %offset.sroa.0.063, -3
  %_32 = icmp ult i64 %_30, %buf.1
  br i1 %_32, label %bb8, label %panic10

panic8:                                           ; preds = %bb2
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %0, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

panic10:                                          ; preds = %bb5
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_30, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

bb8:                                              ; preds = %bb5
  %_29 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_30
  %14 = getelementptr inbounds nuw i8, ptr %13, i64 1
  %_33 = load i8, ptr %14, align 1, !noundef !3
  store i8 %_33, ptr %_29, align 1
  %_37 = add nsw i64 %offset.sroa.0.063, -2
  %_39 = icmp ult i64 %_37, %buf.1
  br i1 %_39, label %bb9, label %panic12

bb9:                                              ; preds = %bb8
  %15 = shl nuw nsw i16 %_2144, 1
  %_41 = zext nneg i16 %15 to i64
  %_36 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_37
  %16 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_41
  %_40 = load i8, ptr %16, align 1, !noundef !3
  store i8 %_40, ptr %_36, align 1
  %_44 = add nsw i64 %offset.sroa.0.063, -1
  %_46 = icmp ult i64 %_44, %buf.1
  br i1 %_46, label %bb12, label %panic14

panic12:                                          ; preds = %bb8
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_37, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

panic14:                                          ; preds = %bb9
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_44, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #24
  unreachable

bb12:                                             ; preds = %bb9
  %_43 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_44
  %17 = getelementptr inbounds nuw i8, ptr %16, i64 1
  %_47 = load i8, ptr %17, align 1, !noundef !3
  store i8 %_47, ptr %_43, align 1
  %_4 = icmp ugt i64 %remain.sroa.0.062, 9999999
  br i1 %_4, label %bb2, label %bb13
}

; Function Attrs: cold minsize noinline noreturn nounwind nonlazybind optsize uwtable
define internal void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %0, i64 noundef %1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %2) unnamed_addr #11 {
start:
  %args = alloca [32 x i8], align 8
  %len = alloca [8 x i8], align 8
  %index = alloca [8 x i8], align 8
  store i64 %0, ptr %index, align 8
  store i64 %1, ptr %len, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %args)
  store ptr %len, ptr %args, align 8
  %_8.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %args, i64 8
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_8.sroa.4.0..sroa_idx, align 8
  %3 = getelementptr inbounds nuw i8, ptr %args, i64 16
  store ptr %index, ptr %3, align 8
  %_9.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %args, i64 24
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_9.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_a434a7e153ac922489f2ed192aeda5a5, ptr noundef nonnull %args, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %2) #24
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt(ptr noalias noundef readonly align 8 captures(none) dereferenceable(8) %self, ptr noalias noundef align 8 captures(none) dereferenceable(24) %f) unnamed_addr #1 {
start:
  %buf = alloca [20 x i8], align 1
  call void @llvm.lifetime.start.p0(i64 20, ptr nonnull %buf)
  %_5 = load i64, ptr %self, align 8, !noundef !3
  %offset.i = call fastcc noundef i64 @_RNvMsf_NtNtNtCscliFh4jUES5_4core3fmt3num3impy10__fmt_inner(i64 noundef %_5, ptr noalias noundef nonnull align 1 %buf, i64 noundef 20) #17
  %_8.i.i = sub nuw nsw i64 20, %offset.i
  %_10.i.i = getelementptr inbounds nuw i8, ptr %buf, i64 %offset.i
  %_0 = call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter12pad_integral(ptr noalias noundef nonnull align 8 dereferenceable(24) %f, i1 noundef zeroext true, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) inttoptr (i64 1 to ptr), i64 noundef 0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_10.i.i, i64 noundef %_8.i.i) #17
  call void @llvm.lifetime.end.p0(i64 20, ptr nonnull %buf)
  ret i1 %_0
}

; Function Attrs: noinline nounwind nonlazybind uwtable
define internal fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(24) %f, i32 noundef range(i32 43, 1114113) %0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %1, i64 %2) unnamed_addr #12 {
start:
  %.not = icmp eq i32 %0, 1114112
  br i1 %.not, label %bb4, label %bb1

bb1:                                              ; preds = %start
  %_9.0 = load ptr, ptr %f, align 8, !nonnull !3, !align !4, !noundef !3
  %3 = getelementptr inbounds nuw i8, ptr %f, i64 8
  %_9.1 = load ptr, ptr %3, align 8, !nonnull !3, !align !5, !noundef !3
  %4 = getelementptr inbounds nuw i8, ptr %_9.1, i64 32
  %5 = load ptr, ptr %4, align 8, !invariant.load !3, !nonnull !3
  %_6 = tail call noundef zeroext i1 %5(ptr noundef nonnull align 1 %_9.0, i32 noundef %0) #22
  br i1 %_6, label %bb7, label %bb4

bb4:                                              ; preds = %bb1, %start
  %.not3 = icmp eq ptr %1, null
  br i1 %.not3, label %bb7, label %bb5

bb7:                                              ; preds = %bb5, %bb4, %bb1
  %_0.sroa.0.0 = phi i1 [ %9, %bb5 ], [ true, %bb1 ], [ false, %bb4 ]
  ret i1 %_0.sroa.0.0

bb5:                                              ; preds = %bb4
  %_10.0 = load ptr, ptr %f, align 8, !nonnull !3, !align !4, !noundef !3
  %6 = getelementptr inbounds nuw i8, ptr %f, i64 8
  %_10.1 = load ptr, ptr %6, align 8, !nonnull !3, !align !5, !noundef !3
  %7 = getelementptr inbounds nuw i8, ptr %_10.1, i64 24
  %8 = load ptr, ptr %7, align 8, !invariant.load !3, !nonnull !3
  %9 = tail call noundef zeroext i1 %8(ptr noundef nonnull align 1 %_10.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %1, i64 noundef %2) #22
  br label %bb7
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #13

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %start1, i64 noundef %end, i64 noundef %len, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) unnamed_addr #9 {
start:
  %_77 = alloca [32 x i8], align 8
  %_57 = alloca [32 x i8], align 8
  %_37 = alloca [32 x i8], align 8
  %_17 = alloca [32 x i8], align 8
  %_14 = alloca [8 x i8], align 8
  %_13 = alloca [8 x i8], align 8
  %_12 = alloca [8 x i8], align 8
  %_11 = alloca [8 x i8], align 8
  %_9 = alloca [8 x i8], align 8
  %_8 = alloca [8 x i8], align 8
  %_6 = alloca [8 x i8], align 8
  %_5 = alloca [8 x i8], align 8
  %_4 = icmp ugt i64 %start1, %len
  br i1 %_4, label %bb1, label %bb2

bb2:                                              ; preds = %start
  %_7 = icmp ugt i64 %end, %len
  br i1 %_7, label %bb3, label %bb4

bb1:                                              ; preds = %start
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_5)
  store i64 %start1, ptr %_5, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_6)
  store i64 %len, ptr %_6, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %_17)
  store ptr %_5, ptr %_17, align 8
  %_18.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_17, i64 8
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_18.sroa.4.0..sroa_idx, align 8
  %1 = getelementptr inbounds nuw i8, ptr %_17, i64 16
  store ptr %_6, ptr %1, align 8
  %_19.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_17, i64 24
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_19.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_9fe2a66cc3026f503dfb4e2696ddcf9c, ptr noundef nonnull %_17, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #24
  unreachable

bb4:                                              ; preds = %bb2
  %_10 = icmp ugt i64 %start1, %end
  br i1 %_10, label %bb5, label %bb6

bb3:                                              ; preds = %bb2
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_8)
  store i64 %end, ptr %_8, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_9)
  store i64 %len, ptr %_9, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %_37)
  store ptr %_8, ptr %_37, align 8
  %_38.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_37, i64 8
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_38.sroa.4.0..sroa_idx, align 8
  %2 = getelementptr inbounds nuw i8, ptr %_37, i64 16
  store ptr %_9, ptr %2, align 8
  %_39.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_37, i64 24
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_39.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_f60af4dda577965ed7990eef71c78b71, ptr noundef nonnull %_37, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #24
  unreachable

bb6:                                              ; preds = %bb4
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_13)
  store i64 %end, ptr %_13, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_14)
  store i64 %len, ptr %_14, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %_77)
  store ptr %_13, ptr %_77, align 8
  %_78.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_77, i64 8
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_78.sroa.4.0..sroa_idx, align 8
  %3 = getelementptr inbounds nuw i8, ptr %_77, i64 16
  store ptr %_14, ptr %3, align 8
  %_79.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_77, i64 24
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_79.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_f60af4dda577965ed7990eef71c78b71, ptr noundef nonnull %_77, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #24
  unreachable

bb5:                                              ; preds = %bb4
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_11)
  store i64 %start1, ptr %_11, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %_12)
  store i64 %end, ptr %_12, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %_57)
  store ptr %_11, ptr %_57, align 8
  %_58.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_57, i64 8
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_58.sroa.4.0..sroa_idx, align 8
  %4 = getelementptr inbounds nuw i8, ptr %_57, i64 16
  store ptr %_12, ptr %4, align 8
  %_59.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_57, i64 24
  store ptr @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt, ptr %_59.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_bfe1e99c4b1e5a3c7be66967fd889b22, ptr noundef nonnull %_57, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #24
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter9write_str(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %self, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %data.0, i64 noundef %data.1) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !3, !align !4, !noundef !3
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load ptr, ptr %0, align 8, !nonnull !3, !align !5, !noundef !3
  %1 = getelementptr inbounds nuw i8, ptr %_3.1, i64 24
  %2 = load ptr, ptr %1, align 8, !invariant.load !3, !nonnull !3
  %_0 = tail call noundef zeroext i1 %2(ptr noundef nonnull align 1 %_3.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %data.0, i64 noundef %data.1) #22
  ret i1 %_0
}

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtCscliFh4jUES5_4core6result13unwrap_failed(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1, ptr noundef nonnull align 1 %2, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(32) %3, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %4) unnamed_addr #9 {
start:
  %args = alloca [32 x i8], align 8
  %error = alloca [16 x i8], align 8
  %msg = alloca [16 x i8], align 8
  store ptr %0, ptr %msg, align 8
  %5 = getelementptr inbounds nuw i8, ptr %msg, i64 8
  store i64 %1, ptr %5, align 8
  store ptr %2, ptr %error, align 8
  %6 = getelementptr inbounds nuw i8, ptr %error, i64 8
  store ptr %3, ptr %6, align 8
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %args)
  store ptr %msg, ptr %args, align 8
  %_8.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %args, i64 8
  store ptr @_RNvXs1i_NtCscliFh4jUES5_4core3fmtReNtB6_7Display3fmtB8_, ptr %_8.sroa.4.0..sroa_idx, align 8
  %7 = getelementptr inbounds nuw i8, ptr %args, i64 16
  store ptr %error, ptr %7, align 8
  %_9.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %args, i64 24
  store ptr @_RNvXs1g_NtCscliFh4jUES5_4core3fmtRDNtB6_5DebugEL_Bx_3fmtB8_, ptr %_9.sroa.4.0..sroa_idx, align 8
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_850f178d3cd0d4307d8d45b841b2c2ad, ptr noundef nonnull %args, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %4) #24
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef ptr @unir_host_alloc(i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
declare void @unir_host_free(ptr noundef, i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: cold nofree noreturn nounwind nonlazybind uwtable
define internal void @_RNvCs1Y7DaGC1cwg_7___rustc17rust_begin_unwind(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %_1) unnamed_addr #14 {
start:
  tail call void @abort() #21
  unreachable
}

; Function Attrs: cold nofree noreturn nounwind nonlazybind uwtable
declare void @abort() unnamed_addr #14

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -5, 1) i64 @unir_consumer_cancel(ptr noundef captures(address, read_provenance) %c) unnamed_addr #1 {
start:
  %err.i = alloca [16 x i8], align 8
  %_5 = alloca [88 x i8], align 8
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5)
  %0 = getelementptr inbounds nuw i8, ptr %_5, i64 24
  %1 = getelementptr inbounds nuw i8, ptr %_5, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %0, i8 0, i64 32, i1 false)
  store i64 65536, ptr %1, align 8
  %2 = getelementptr inbounds nuw i8, ptr %_5, i64 64
  store i64 0, ptr %_5, align 8
  %_11.sroa.4.0._5.sroa_idx = getelementptr inbounds nuw i8, ptr %_5, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %2, i8 0, i64 24, i1 false)
  store ptr inttoptr (i64 8 to ptr), ptr %_11.sroa.4.0._5.sroa_idx, align 8
  %_11.sroa.5.0._5.sroa_idx = getelementptr inbounds nuw i8, ptr %_5, i64 16
  store i64 0, ptr %_11.sroa.5.0._5.sroa_idx, align 8
  tail call void @llvm.experimental.noalias.scope.decl(metadata !91)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i)
  %3 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %4 = load i8, ptr %3, align 4, !range !94, !alias.scope !91, !noalias !95, !noundef !3
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb8.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3: ; preds = %start
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  br label %bb6

bb8.i:                                            ; preds = %start
  %_13 = getelementptr inbounds nuw i8, ptr %c, i64 16
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_13, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5, i8 noundef 11, i8 noundef 3, i8 undef) #17, !noalias !98
  %5 = load i8, ptr %err.i, align 8, !range !94, !noalias !99, !noundef !3
  %6 = icmp eq i8 %5, 0
  br i1 %6, label %bb4.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb8.i
  %7 = getelementptr inbounds nuw i8, ptr %err.i, i64 1
  %8 = load i8, ptr %7, align 1, !range !100, !noalias !99, !noundef !3
  %_21.i9 = icmp ne i8 %8, 3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  %. = sext i1 %_21.i9 to i64
  br label %bb6

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb8.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  %switch.tableidx = add nsw i8 %5, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6

bb6:                                              ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, %bb4.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3
  %_0.sroa.0.0 = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3 ], [ %., %bb4.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit ]
  call void @llvm.experimental.noalias.scope.decl(metadata !101)
  call void @llvm.experimental.noalias.scope.decl(metadata !104)
  call void @llvm.experimental.noalias.scope.decl(metadata !107)
  call void @llvm.experimental.noalias.scope.decl(metadata !110)
  call void @llvm.experimental.noalias.scope.decl(metadata !113)
  %_9.i.i.i.i.i = load i64, ptr %_5, align 8, !range !116, !alias.scope !117, !noundef !3
  %9 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %9, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb6
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_11.sroa.4.0._5.sroa_idx, align 8, !alias.scope !117, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !117
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb6
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(40) %self, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef range(i8 0, 12) %0, i8 noundef range(i8 0, 4) %1, i8 %2) unnamed_addr #1 {
start:
  %3 = getelementptr inbounds nuw i8, ptr %self, i64 38
  %4 = load i8, ptr %3, align 2, !range !118, !noundef !3
  %_7 = trunc nuw i8 %4 to i1
  %5 = add nsw i8 %0, -10
  %6 = icmp samesign ugt i8 %0, 9
  %narrow7 = select i1 %6, i8 %5, i8 2
  br i1 %_7, label %bb2, label %bb4

bb2:                                              ; preds = %start
  switch i8 %narrow7, label %bb1 [
    i8 1, label %bb5
    i8 0, label %bb13
    i8 2, label %bb14
  ]

bb4:                                              ; preds = %start
  switch i8 %narrow7, label %bb1 [
    i8 0, label %bb17
    i8 1, label %bb19
    i8 2, label %bb18
  ]

bb1:                                              ; preds = %bb4, %bb2
  unreachable

bb19:                                             ; preds = %bb4
  br label %bb17

bb18:                                             ; preds = %bb4
  %_26 = zext nneg i8 %0 to i32
  %_25 = shl nuw nsw i32 %_26, 8
  %7 = or disjoint i32 %_25, 2
  br label %bb17

bb17:                                             ; preds = %bb18, %bb19, %bb4
  %_10.sroa.0.0 = phi i32 [ 1, %bb19 ], [ %7, %bb18 ], [ 0, %bb4 ]
  %8 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 8, i32 noundef %_10.sroa.0.0) #17
  br label %bb8

bb8:                                              ; preds = %bb13, %bb5, %bb17
  %stored.sroa.0.0 = phi i8 [ %9, %bb5 ], [ %11, %bb13 ], [ %8, %bb17 ]
  switch i8 %stored.sroa.0.0, label %bb12 [
    i8 7, label %bb10
    i8 3, label %bb24
    i8 4, label %bb23
  ]

bb5:                                              ; preds = %bb2
  %9 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 128, i64 noundef -1) #17
  br label %bb8

bb14:                                             ; preds = %bb2
  %_21 = zext nneg i8 %0 to i32
  %_20 = shl nuw nsw i32 %_21, 8
  %10 = or disjoint i32 %_20, 2
  br label %bb13

bb13:                                             ; preds = %bb14, %bb2
  %_11.sroa.0.0 = phi i32 [ %10, %bb14 ], [ 0, %bb2 ]
  %11 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 144, i32 noundef %_11.sroa.0.0) #17
  br label %bb8

bb10:                                             ; preds = %bb8
  %..i = select i1 %_7, i64 384, i64 256
  %.13.i = select i1 %_7, i64 260, i64 388
  %12 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %.13.i) #17, !noalias !119
  %.sroa.6.0.extract.shift.i = lshr i64 %12, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %13 = trunc i64 %12 to i1
  br i1 %13, label %bb12, label %bb17.i

bb17.i:                                           ; preds = %bb10
  %_9.i = and i32 %.sroa.6.0.extract.trunc.i, 1
  %14 = icmp ne i32 %_9.i, 0
  %15 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_11.i = load i32, ptr %15, align 8, !alias.scope !122, !noalias !119
  %_10.i = icmp ne i32 %_11.i, %.sroa.6.0.extract.trunc.i
  %or.cond.i = select i1 %14, i1 %_10.i, i1 false
  br i1 %or.cond.i, label %bb7.i, label %bb12

bb7.i:                                            ; preds = %bb17.i
  store i32 %.sroa.6.0.extract.trunc.i, ptr %15, align 8, !alias.scope !122, !noalias !119
  %16 = getelementptr inbounds nuw i8, ptr %self, i64 24
  %_13.i = load i32, ptr %16, align 8, !alias.scope !122, !noalias !119, !noundef !3
  %_12.i = add i32 %_13.i, 1
  store i32 %_12.i, ptr %16, align 8, !alias.scope !122, !noalias !119
  %17 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %..i, i32 noundef %_12.i) #17, !noalias !119
  %.not.i = icmp eq i8 %17, 7
  br i1 %.not.i, label %bb19.i, label %bb12

bb19.i:                                           ; preds = %bb7.i
  %18 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %..i, i32 noundef 1) #17
  br label %bb12

bb12:                                             ; preds = %bb23, %bb24, %bb19.i, %bb7.i, %bb17.i, %bb10, %bb8
  %ended.sroa.4.0 = phi i8 [ %2, %bb17.i ], [ %2, %bb7.i ], [ 6, %bb24 ], [ 8, %bb23 ], [ 2, %bb8 ], [ %2, %bb10 ], [ %2, %bb19.i ]
  %ended.sroa.0.0 = phi i8 [ %1, %bb17.i ], [ %1, %bb7.i ], [ %4, %bb24 ], [ %4, %bb23 ], [ %4, %bb8 ], [ %1, %bb10 ], [ %1, %bb19.i ]
  %19 = getelementptr inbounds nuw i8, ptr %self, i64 36
  store i8 %ended.sroa.0.0, ptr %19, align 4
  %20 = getelementptr inbounds nuw i8, ptr %self, i64 37
  store i8 %ended.sroa.4.0, ptr %20, align 1
  %21 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 %ended.sroa.0.0, ptr %21, align 1
  %22 = getelementptr inbounds nuw i8, ptr %_0, i64 2
  store i8 %ended.sroa.4.0, ptr %22, align 2
  store i8 0, ptr %_0, align 8
  ret void

bb24:                                             ; preds = %bb8
  br label %bb12

bb23:                                             ; preds = %bb8
  br label %bb12
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: read) uwtable
define noundef range(i32 0, 1024) i32 @unir_consumer_ended(ptr noundef readonly captures(none) %c) unnamed_addr #15 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %1 = load i8, ptr %0, align 4, !range !94, !noundef !3
  %2 = getelementptr inbounds nuw i8, ptr %c, i64 53
  %3 = load i8, ptr %2, align 1
  %.not = icmp eq i8 %1, 4
  br i1 %.not, label %bb1, label %bb3

bb3:                                              ; preds = %start
  %4 = add nsw i8 %1, -2
  %.inv = icmp samesign ult i8 %1, 2
  %narrow = select i1 %.inv, i8 2, i8 %4
  switch i8 %narrow, label %bb2 [
    i8 0, label %bb1
    i8 1, label %bb5
    i8 2, label %bb4
  ]

bb1:                                              ; preds = %bb4, %bb5, %bb3, %start
  %_0.sroa.0.0 = phi i32 [ 2, %bb5 ], [ %8, %bb4 ], [ 0, %start ], [ 1, %bb3 ]
  ret i32 %_0.sroa.0.0

bb2:                                              ; preds = %bb3
  unreachable

bb5:                                              ; preds = %bb3
  br label %bb1

bb4:                                              ; preds = %bb3
  %_9 = zext i8 %3 to i32
  %_8 = shl nuw nsw i32 %_9, 2
  %5 = shl nuw i8 %1, 6
  %6 = and i8 %5, 64
  %7 = or disjoint i8 %6, 3
  %_7 = zext nneg i8 %7 to i32
  %8 = or i32 %_8, %_7
  br label %bb1
}

; Function Attrs: nounwind nonlazybind uwtable
define void @unir_consumer_free(ptr noundef %c) unnamed_addr #1 {
start:
  %0 = icmp ne ptr %c, null
  tail call void @llvm.assume(i1 %0)
  tail call void @unir_host_free(ptr noundef nonnull %c, i64 noundef 112, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !124
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define noalias noundef ptr @unir_consumer_open(ptr noundef %vat, i64 noundef %0, i32 noundef %1, i32 noundef %2) unnamed_addr #1 {
start:
  %_15.i.i = alloca [16 x i8], align 8
  %c.i.i = alloca [112 x i8], align 8
  %_16.sroa.9.i = alloca [103 x i8], align 1
  %_9.i = alloca [24 x i8], align 8
  %_14.sroa.5 = alloca [103 x i8], align 1
  %.not.i.i.i = icmp ne i32 %1, 0
  %3 = icmp ugt i32 %2, 8
  %or.cond.i.i.i = and i1 %.not.i.i.i, %3
  %4 = and i32 %2, 7
  %5 = icmp eq i32 %4, 0
  %or.cond8.i.i.i = and i1 %or.cond.i.i.i, %5
  br i1 %or.cond8.i.i.i, label %bb9.i, label %bb2

bb9.i:                                            ; preds = %start
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_9.i), !noalias !129
  %or.cond.i = icmp ugt i64 %0, 4294967295
  br i1 %or.cond.i, label %bb11.i, label %bb12.i

bb11.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !129
  br label %bb2

bb12.i:                                           ; preds = %bb9.i
  %6 = icmp ne ptr %vat, null
  tail call void @llvm.assume(i1 %6)
  %_26.i = trunc nuw i64 %0 to i32
  call void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr noalias noundef nonnull sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_9.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i32 noundef %_26.i) #17, !noalias !129
  %7 = load i8, ptr %_9.i, align 8, !range !118, !noalias !129, !noundef !3
  %8 = trunc nuw i8 %7 to i1
  br i1 %8, label %bb13.i, label %bb14.i

bb13.i:                                           ; preds = %bb12.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !129
  br label %bb2

bb14.i:                                           ; preds = %bb12.i
  %9 = getelementptr inbounds nuw i8, ptr %_9.i, i64 8
  %_30.0.i = load i64, ptr %9, align 8, !noalias !129, !noundef !3
  %10 = getelementptr inbounds nuw i8, ptr %_9.i, i64 16
  %_30.1.i = load i64, ptr %10, align 8, !noalias !129, !noundef !3
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !129
  call void @llvm.lifetime.start.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  %_20.i.i = zext i32 %1 to i64
  %_22.i.i = zext i32 %2 to i64
  %_19.i.i = mul nuw i64 %_22.i.i, %_20.i.i
  %_7.i.i = add nuw i64 %_19.i.i, 512
  %_4.i.i = icmp ult i64 %_30.1.i, %_7.i.i
  br i1 %_4.i.i, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, label %bb3.i.i

bb3.i.i:                                          ; preds = %bb14.i
  call void @llvm.lifetime.start.p0(i64 112, ptr nonnull %c.i.i), !noalias !133
  %11 = lshr i64 %_20.i.i, 2
  %spec.store.select.i.i = tail call i64 @llvm.umax.i64(i64 %11, i64 1)
  %12 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 16
  store i64 %_30.0.i, ptr %12, align 8, !noalias !133
  %_10.sroa.4.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 24
  store i64 %_30.1.i, ptr %_10.sroa.4.0..sroa_idx.i.i, align 8, !noalias !133
  %_10.sroa.5.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 32
  store i32 %1, ptr %_10.sroa.5.0..sroa_idx.i.i, align 8, !noalias !133
  %_10.sroa.6.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 36
  store i32 %2, ptr %_10.sroa.6.0..sroa_idx.i.i, align 4, !noalias !133
  %_10.sroa.7.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 40
  store i32 0, ptr %_10.sroa.7.0..sroa_idx.i.i, align 8, !noalias !133
  %_10.sroa.8.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 44
  store i32 0, ptr %_10.sroa.8.0..sroa_idx.i.i, align 4, !noalias !133
  %_10.sroa.9.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 48
  store i32 0, ptr %_10.sroa.9.0..sroa_idx.i.i, align 8, !noalias !133
  %_10.sroa.10.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 52
  store i8 4, ptr %_10.sroa.10.0..sroa_idx.i.i, align 4, !noalias !133
  %_10.sroa.12.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 54
  store i8 1, ptr %_10.sroa.12.0..sroa_idx.i.i, align 2, !noalias !133
  %13 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 56
  %14 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 72
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %13, i8 0, i64 16, i1 false), !noalias !133
  store i64 %spec.store.select.i.i, ptr %14, align 8, !noalias !133
  %15 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 80
  store i64 512, ptr %15, align 8, !noalias !133
  %16 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 88
  store i64 0, ptr %c.i.i, align 8, !noalias !133
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(17) %16, i8 0, i64 17, i1 false), !noalias !133
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_15.i.i), !noalias !133
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_15.i.i, ptr noalias noundef align 8 dereferenceable(112) %c.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat) #17, !noalias !137
  %17 = load i8, ptr %_15.i.i, align 8, !range !138, !noalias !133, !noundef !3
  %.not.i.i = icmp eq i8 %17, 5
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_15.i.i), !noalias !133
  br i1 %.not.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb11.i.i

bb11.i.i:                                         ; preds = %bb3.i.i
  call void @llvm.lifetime.end.p0(i64 112, ptr nonnull %c.i.i), !noalias !133
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb3.i.i
  %_16.sroa.0.0.copyload1.i = load i64, ptr %c.i.i, align 8, !noalias !139
  %_16.sroa.7.0.c.i.sroa_idx.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 8
  %_16.sroa.7.0.copyload2.i = load i8, ptr %_16.sroa.7.0.c.i.sroa_idx.i, align 8, !noalias !139
  %_16.sroa.9.0.c.i.sroa_idx.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 9
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.i, ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.0.c.i.sroa_idx.i, i64 103, i1 false), !noalias !139
  call void @llvm.lifetime.end.p0(i64 112, ptr nonnull %c.i.i), !noalias !133
  %18 = icmp eq i64 %_16.sroa.0.0.copyload1.i, 2
  br i1 %18, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, label %bb5

_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6: ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb11.i.i, %bb14.i
  call void @llvm.lifetime.end.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  br label %bb2

bb5:                                              ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  call void @llvm.lifetime.start.p0(i64 103, ptr nonnull %_14.sroa.5)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(103) %_14.sroa.5, ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.i, i64 103, i1 false)
  call void @llvm.lifetime.end.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #17
  %_0.i.i.i = call noalias noundef ptr @unir_host_alloc(i64 noundef 112, i64 noundef range(i64 1, -9223372036854775807) 8) #17
  %19 = icmp eq ptr %_0.i.i.i, null
  br i1 %19, label %bb6, label %bb7, !prof !140

bb2:                                              ; preds = %bb7, %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, %bb13.i, %bb11.i, %start
  %_0.sroa.0.0 = phi ptr [ %_0.i.i.i, %bb7 ], [ null, %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6 ], [ null, %bb11.i ], [ null, %bb13.i ], [ null, %start ]
  ret ptr %_0.sroa.0.0

bb6:                                              ; preds = %bb5
  call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 112) #21
  unreachable

bb7:                                              ; preds = %bb5
  store i64 %_16.sroa.0.0.copyload1.i, ptr %_0.i.i.i, align 8
  %_14.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 8
  store i8 %_16.sroa.7.0.copyload2.i, ptr %_14.sroa.4.0..sroa_idx, align 8
  %_14.sroa.5.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 9
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(103) %_14.sroa.5.0..sroa_idx, ptr noundef nonnull align 1 dereferenceable(103) %_14.sroa.5, i64 103, i1 false)
  call void @llvm.lifetime.end.p0(i64 103, ptr nonnull %_14.sroa.5)
  br label %bb2
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(112) %self, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #1 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 64
  %_4 = load i64, ptr %0, align 8, !noundef !3
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_6 = load i32, ptr %2, align 8, !noundef !3
  %_5 = zext i32 %_6 to i64
  %_3 = add i64 %_4, %_5
  %3 = load i64, ptr %self, align 8, !range !141, !noundef !3
  %4 = trunc nuw i64 %3 to i1
  %5 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %6 = load i64, ptr %5, align 8
  %7 = tail call i64 @llvm.umin.i64(i64 %6, i64 %_3)
  %spec.store.select = select i1 %4, i64 %7, i64 %_3
  %8 = getelementptr inbounds nuw i8, ptr %self, i64 88
  %_11 = load i64, ptr %8, align 8, !noundef !3
  %_9.not = icmp ugt i64 %spec.store.select, %_11
  br i1 %_9.not, label %bb2, label %bb1

bb2:                                              ; preds = %start
  store i64 %spec.store.select, ptr %8, align 8
  %9 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 136, i64 noundef %spec.store.select) #17
  %.not = icmp eq i8 %9, 7
  %10 = getelementptr inbounds nuw i8, ptr %self, i64 54
  %11 = load i8, ptr %10, align 2, !range !118
  br i1 %.not, label %bb17, label %bb5

bb1:                                              ; preds = %start
  store i8 5, ptr %_0, align 8
  br label %bb8

bb17:                                             ; preds = %bb2
  %12 = trunc nuw i8 %11 to i1
  %..i = select i1 %12, i64 384, i64 256
  %.13.i = select i1 %12, i64 260, i64 388
  %13 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %.13.i) #17, !noalias !142
  %.sroa.6.0.extract.shift.i = lshr i64 %13, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %14 = trunc i64 %13 to i1
  br i1 %14, label %bb15, label %bb17.i

bb17.i:                                           ; preds = %bb17
  %_9.i = and i32 %.sroa.6.0.extract.trunc.i, 1
  %15 = icmp ne i32 %_9.i, 0
  %16 = getelementptr inbounds nuw i8, ptr %self, i64 48
  %_11.i = load i32, ptr %16, align 8, !alias.scope !145, !noalias !142
  %_10.i = icmp ne i32 %_11.i, %.sroa.6.0.extract.trunc.i
  %or.cond.i = select i1 %15, i1 %_10.i, i1 false
  br i1 %or.cond.i, label %bb7.i, label %bb6

bb7.i:                                            ; preds = %bb17.i
  store i32 %.sroa.6.0.extract.trunc.i, ptr %16, align 8, !alias.scope !145, !noalias !142
  %17 = getelementptr inbounds nuw i8, ptr %self, i64 40
  %_13.i = load i32, ptr %17, align 8, !alias.scope !145, !noalias !142, !noundef !3
  %_12.i = add i32 %_13.i, 1
  store i32 %_12.i, ptr %17, align 8, !alias.scope !145, !noalias !142
  %18 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %..i, i32 noundef %_12.i) #17, !noalias !142
  %.not.i = icmp eq i8 %18, 7
  br i1 %.not.i, label %bb19.i, label %bb5

bb19.i:                                           ; preds = %bb7.i
  %19 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %..i, i32 noundef 1) #17
  %20 = trunc i64 %19 to i1
  br i1 %20, label %bb15, label %bb6

bb15:                                             ; preds = %bb19.i, %bb17
  %_14.sroa.0.0.in.in = phi i64 [ %13, %bb17 ], [ %19, %bb19.i ]
  %_14.sroa.0.0.in = lshr i64 %_14.sroa.0.0.in.in, 8
  %_14.sroa.0.0 = trunc i64 %_14.sroa.0.0.in to i8
  %.not8 = icmp eq i8 %_14.sroa.0.0, 7
  br i1 %.not8, label %bb6, label %bb5

bb5:                                              ; preds = %bb15, %bb7.i, %bb2
  %_14.sroa.0.017 = phi i8 [ %_14.sroa.0.0, %bb15 ], [ %18, %bb7.i ], [ %9, %bb2 ]
  %switch.selectcmp = icmp eq i8 %_14.sroa.0.017, 4
  %switch.select = select i1 %switch.selectcmp, i8 8, i8 2
  %switch.selectcmp9 = icmp eq i8 %_14.sroa.0.017, 3
  %switch.select10 = select i1 %switch.selectcmp9, i8 6, i8 %switch.select
  tail call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef align 8 dereferenceable(88) %s, i8 noundef %switch.select10, i8 noundef %11, i8 %switch.select10) #17
  br label %bb8

bb6:                                              ; preds = %bb15, %bb19.i, %bb17.i
  store i8 5, ptr %_0, align 8
  br label %bb8

bb8:                                              ; preds = %bb6, %bb5, %bb1
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef i64 @unir_consumer_read(ptr noundef captures(address, read_provenance) %c, ptr noundef %buf, i64 noundef %cap, i64 noundef %timeout_ns, ptr noundef writeonly captures(address_is_null) %substream) unnamed_addr #1 {
start:
  %_38.i = alloca [16 x i8], align 8
  %_24.i = alloca [24 x i8], align 8
  %_7.i = alloca [24 x i8], align 8
  %attempt.i = alloca [16 x i8], align 8
  %_10 = alloca [88 x i8], align 8
  %_8 = alloca [24 x i8], align 8
  %0 = icmp eq i64 %cap, 0
  %.buf = select i1 %0, ptr inttoptr (i64 1 to ptr), ptr %buf
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_8)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_10)
  %1 = getelementptr inbounds nuw i8, ptr %_10, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_10, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false)
  store i64 65536, ptr %2, align 8
  %3 = getelementptr inbounds nuw i8, ptr %_10, i64 64
  store i64 0, ptr %_10, align 8
  %_21.sroa.4.0._10.sroa_idx = getelementptr inbounds nuw i8, ptr %_10, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false)
  store ptr inttoptr (i64 8 to ptr), ptr %_21.sroa.4.0._10.sroa_idx, align 8
  %_21.sroa.5.0._10.sroa_idx = getelementptr inbounds nuw i8, ptr %_10, i64 16
  store i64 0, ptr %_21.sroa.5.0._10.sroa_idx, align 8
  %_23 = icmp sgt i64 %timeout_ns, -1
  %_11.sroa.0.0 = zext i1 %_23 to i64
  tail call void @llvm.experimental.noalias.scope.decl(metadata !147)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !150)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %attempt.i)
  store ptr %.buf, ptr %attempt.i, align 8, !noalias !152
  %4 = getelementptr inbounds nuw i8, ptr %attempt.i, i64 8
  store i64 %cap, ptr %4, align 8, !noalias !152
  %_45.sroa.5.0._7.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  %_45.sroa.6.0._7.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 16
  %_0.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 16
  %5 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %6 = getelementptr inbounds nuw i8, ptr %c, i64 44
  %_60.sroa.5.0._24.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_24.i, i64 8
  %_60.sroa.6.0._24.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_24.i, i64 16
  br label %bb26.i

bb27.i:                                           ; preds = %bb5.i
  %7 = load i8, ptr %5, align 2, !range !118, !alias.scope !155, !noalias !158, !noundef !3
  %_8.i.i = load i32, ptr %6, align 4, !alias.scope !155, !noalias !158, !noundef !3
  %_7.i.i = and i32 %_8.i.i, 1
  %_6.i.not.i = icmp eq i32 %_7.i.i, 0
  br i1 %_6.i.not.i, label %bb5.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

bb5.i.i:                                          ; preds = %bb27.i
  %_9.i.i = or disjoint i32 %_8.i.i, 1
  store i32 %_9.i.i, ptr %6, align 4, !alias.scope !155, !noalias !158
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb5.i.i, %bb27.i
  %_12.i.i = phi i32 [ %_8.i.i, %bb27.i ], [ %_9.i.i, %bb5.i.i ]
  %8 = trunc nuw i8 %7 to i1
  %..i.i = select i1 %8, i64 388, i64 260
  %_0.i.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i, i32 noundef %_12.i.i) #17, !noalias !147
  %.not.i = icmp eq i8 %_0.i.i, 7
  br i1 %.not.i, label %bb32.i, label %bb9.i

bb26.i:                                           ; preds = %bb26.i.backedge, %start
  %iter.sroa.0.051.i = phi i64 [ 0, %start ], [ %iter.sroa.0.051.i.be, %bb26.i.backedge ]
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_7.i), !noalias !152
  call fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([24 x i8]) align 8 captures(address) dereferenceable(24) %_7.i, ptr noalias noundef align 8 dereferenceable(16) %attempt.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #22, !noalias !147
  %9 = load i64, ptr %_7.i, align 8, !range !159, !noalias !152, !noundef !3
  %10 = icmp eq i64 %9, 2
  %_46.sroa.0.0.copyload.i = load i64, ptr %_45.sroa.5.0._7.sroa_idx.i, align 8, !noalias !152
  %_46.sroa.5.0.copyload.i = load i16, ptr %_45.sroa.6.0._7.sroa_idx.i, align 8, !noalias !152
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !152
  %11 = trunc nuw i64 %9 to i1
  %or.cond = select i1 %10, i1 true, i1 %11
  br i1 %or.cond, label %bb25.i, label %bb5.i

bb32.i:                                           ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %..i = select i1 %8, i64 256, i64 384
  %12 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %_0.i.i.i, i64 noundef %..i) #17, !noalias !147
  %.sroa.6.0.extract.shift.i = lshr i64 %12, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %13 = trunc i64 %12 to i1
  br i1 %13, label %bb9.split.loop.exit52.i, label %bb10.i

bb9.split.loop.exit52.i:                          ; preds = %bb32.i
  %.sroa.4.0.extract.shift.le.i = lshr i64 %12, 8
  %.sroa.4.0.extract.trunc.le.i = trunc i64 %.sroa.4.0.extract.shift.le.i to i8
  br label %bb9.i

bb9.i:                                            ; preds = %bb9.split.loop.exit52.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %_16.sroa.6.0.i = phi i8 [ %.sroa.4.0.extract.trunc.le.i, %bb9.split.loop.exit52.i ], [ %_0.i.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i ]
  %switch.selectcmp.i = icmp eq i8 %_16.sroa.6.0.i, 4
  %switch.select.i = select i1 %switch.selectcmp.i, i8 8, i8 2
  %switch.selectcmp26.i = icmp eq i8 %_16.sroa.6.0.i, 3
  %switch.select27.i = select i1 %switch.selectcmp26.i, i8 6, i8 %switch.select.i
  %14 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %14, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select27.i, i8 noundef %7, i8 %switch.select27.i) #17
  br label %bb2.sink.split

bb10.i:                                           ; preds = %bb32.i
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_24.i), !noalias !152
  call fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([24 x i8]) align 8 captures(address) dereferenceable(24) %_24.i, ptr noalias noundef align 8 dereferenceable(16) %attempt.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #22, !noalias !147
  %15 = load i64, ptr %_24.i, align 8, !range !159, !noalias !152, !noundef !3
  %16 = icmp eq i64 %15, 2
  %_61.sroa.0.0.copyload.i = load i64, ptr %_60.sroa.5.0._24.sroa_idx.i, align 8, !noalias !152
  %_61.sroa.5.0.copyload.i = load i16, ptr %_60.sroa.6.0._24.sroa_idx.i, align 8, !noalias !152
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_24.i), !noalias !152
  br i1 %16, label %bb42.i, label %bb43.i

bb42.i:                                           ; preds = %bb10.i
  %17 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_61.sroa.0.0.copyload.i, ptr %17, align 8, !alias.scope !147, !noalias !160
  %_63.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_61.sroa.5.0.copyload.i, ptr %_63.sroa.2.0..sroa_idx.i, align 8, !alias.scope !147, !noalias !160
  br label %bb2.sink.split

bb43.i:                                           ; preds = %bb10.i
  %18 = trunc nuw i64 %15 to i1
  br i1 %18, label %bb13.i, label %bb12.i

bb13.i:                                           ; preds = %bb43.i
  %19 = call fastcc noundef i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, i1 noundef zeroext false) #17, !noalias !147
  %.not25.i = icmp eq i8 %19, 7
  br i1 %.not25.i, label %bb17.i, label %bb16.i

bb12.i:                                           ; preds = %bb43.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_38.i), !noalias !152
  call void @llvm.experimental.noalias.scope.decl(metadata !161)
  call void @llvm.experimental.noalias.scope.decl(metadata !164)
  %20 = load i8, ptr %5, align 2, !range !118, !alias.scope !166, !noalias !167, !noundef !3
  %21 = trunc nuw i8 %20 to i1
  %..i34.i = select i1 %21, i64 256, i64 384
  %22 = call { i1, i8 } @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate4wait(ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i34.i, i32 noundef %.sroa.6.0.extract.trunc.i, i64 noundef range(i64 0, 2) %_11.sroa.0.0, i64 %timeout_ns) #17, !noalias !169
  %23 = extractvalue { i1, i8 } %22, 0
  %24 = extractvalue { i1, i8 } %22, 1
  br i1 %23, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb13.i.i

bb13.i.i:                                         ; preds = %bb12.i
  %_8.i.i.i = load i32, ptr %6, align 4, !alias.scope !170, !noalias !167, !noundef !3
  %_7.i.i.i = and i32 %_8.i.i.i, 1
  %_6.i.not.i.i = icmp eq i32 %_7.i.i.i, 0
  br i1 %_6.i.not.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, label %bb5.i.i.i

bb5.i.i.i:                                        ; preds = %bb13.i.i
  %_9.i.i.i = add i32 %_8.i.i.i, 1
  store i32 %_9.i.i.i, ptr %6, align 4, !alias.scope !170, !noalias !167
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i.i.i, %bb13.i.i
  %_12.i.i.i = phi i32 [ %_8.i.i.i, %bb13.i.i ], [ %_9.i.i.i, %bb5.i.i.i ]
  %..i.i.i = select i1 %21, i64 388, i64 260
  %_0.i.i35.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i.i, i32 noundef %_12.i.i.i) #17, !noalias !169
  %.not.not.i.i = icmp eq i8 %_0.i.i35.i, 7
  br i1 %.not.not.i.i, label %bb6.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %switch.i.i = icmp ult i8 %24, 2
  br i1 %switch.i.i, label %bb50.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i: ; preds = %bb6.i.i
  store i8 1, ptr %_38.i, align 8, !alias.scope !161, !noalias !173
  br label %bb49.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb12.i
  %waited.sroa.4.0.i.i = phi i8 [ %_0.i.i35.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i ], [ %24, %bb12.i ]
  %switch.selectcmp.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_38.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select8.i.i, i8 noundef %20, i8 %switch.select8.i.i) #17, !noalias !147
  %.pr.i = load i8, ptr %_38.i, align 8, !noalias !152
  %.not24.i = icmp eq i8 %.pr.i, 5
  br i1 %.not24.i, label %bb50.i, label %bb49.i

bb16.i:                                           ; preds = %bb13.i
  %switch.selectcmp28.i = icmp eq i8 %19, 4
  %switch.select29.i = select i1 %switch.selectcmp28.i, i8 8, i8 2
  %switch.selectcmp30.i = icmp eq i8 %19, 3
  %switch.select31.i = select i1 %switch.selectcmp30.i, i8 6, i8 %switch.select29.i
  %25 = load i8, ptr %5, align 2, !range !118, !alias.scope !150, !noalias !158, !noundef !3
  %26 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %26, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select31.i, i8 noundef %25, i8 %switch.select31.i) #17
  br label %bb2.sink.split

bb17.i:                                           ; preds = %bb13.i
  %27 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_61.sroa.0.0.copyload.i, ptr %27, align 8, !alias.scope !147, !noalias !160
  %28 = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_61.sroa.5.0.copyload.i, ptr %28, align 8, !alias.scope !147, !noalias !160
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %attempt.i)
  br label %bb3

bb49.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i
  %29 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %29, ptr noundef nonnull align 8 dereferenceable(16) %_38.i, i64 16, i1 false), !noalias !160
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !152
  br label %bb2.sink.split

bb50.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb6.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !152
  br label %bb26.i.backedge

bb5.i:                                            ; preds = %bb26.i
  %_42.i = add nuw nsw i64 %iter.sroa.0.051.i, 1
  call void @llvm.x86.sse2.pause(), !noalias !147
  %exitcond.not.i = icmp eq i64 %_42.i, 64
  br i1 %exitcond.not.i, label %bb27.i, label %bb26.i.backedge

bb26.i.backedge:                                  ; preds = %bb5.i, %bb50.i
  %iter.sroa.0.051.i.be = phi i64 [ %_42.i, %bb5.i ], [ 0, %bb50.i ]
  br label %bb26.i

bb25.i:                                           ; preds = %bb26.i
  %30 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_46.sroa.0.0.copyload.i, ptr %30, align 8, !alias.scope !147, !noalias !160
  %_48.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_46.sroa.5.0.copyload.i, ptr %_48.sroa.2.0..sroa_idx.i, align 8, !alias.scope !147, !noalias !160
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %attempt.i)
  br i1 %10, label %bb2, label %bb3

bb2.sink.split:                                   ; preds = %bb49.i, %bb16.i, %bb42.i, %bb9.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %attempt.i)
  br label %bb2

bb2:                                              ; preds = %bb2.sink.split, %bb25.i
  %31 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  %e.sroa.0.0.copyload = load i8, ptr %31, align 8
  %switch.idx.cast = zext i8 %e.sroa.0.0.copyload to i64
  %switch.offset = xor i64 %switch.idx.cast, -1
  br label %bb7

bb3:                                              ; preds = %bb25.i, %bb17.i
  %32 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  %f = load i64, ptr %32, align 8, !noundef !3
  %33 = icmp eq ptr %substream, null
  br i1 %33, label %bb7, label %bb5

bb5:                                              ; preds = %bb3
  %34 = getelementptr inbounds nuw i8, ptr %_8, i64 16
  %f2 = load i16, ptr %34, align 8, !noundef !3
  store i16 %f2, ptr %substream, align 2
  br label %bb7

bb7:                                              ; preds = %bb5, %bb3, %bb2
  %_0.sroa.0.0 = phi i64 [ %f, %bb3 ], [ %f, %bb5 ], [ %switch.offset, %bb2 ]
  call void @llvm.experimental.noalias.scope.decl(metadata !174)
  call void @llvm.experimental.noalias.scope.decl(metadata !177)
  call void @llvm.experimental.noalias.scope.decl(metadata !180)
  call void @llvm.experimental.noalias.scope.decl(metadata !183)
  call void @llvm.experimental.noalias.scope.decl(metadata !186)
  %_9.i.i.i.i.i = load i64, ptr %_10, align 8, !range !116, !alias.scope !189, !noundef !3
  %35 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %35, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb7
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_21.sroa.4.0._10.sroa_idx, align 8, !alias.scope !189, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !189
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb7
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_10)
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_8)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: inlinehint nounwind nonlazybind uwtable
define internal fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_0, ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(16) %_1, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #16 {
start:
  %_20.i.i = alloca [16 x i8], align 8
  %_8.i.i.i.i = alloca [16 x i8], align 8
  %h.i.i = alloca [8 x i8], align 8
  %_19.i.i = alloca [16 x i8], align 8
  %_10.i.i = alloca [16 x i8], align 8
  %_23.i = alloca [16 x i8], align 8
  %_7.i = alloca [24 x i8], align 8
  %_4.0 = load ptr, ptr %_1, align 8, !nonnull !3, !align !4, !noundef !3
  %0 = getelementptr inbounds nuw i8, ptr %_1, i64 8
  %_4.1 = load i64, ptr %0, align 8, !noundef !3
  tail call void @llvm.experimental.noalias.scope.decl(metadata !190)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !193)
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_7.i), !noalias !195
  tail call void @llvm.experimental.noalias.scope.decl(metadata !198)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !201)
  %1 = getelementptr inbounds nuw i8, ptr %c, i64 16
  %2 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %3 = load i8, ptr %2, align 4, !range !94, !alias.scope !203, !noalias !204, !noundef !3
  %.not10.i.i = icmp eq i8 %3, 4
  br i1 %.not10.i.i, label %bb26.i.i, label %bb27.i.i

bb27.i.i:                                         ; preds = %start
  %4 = getelementptr inbounds nuw i8, ptr %c, i64 53
  %_46.1.i.i = load i8, ptr %4, align 1, !alias.scope !203, !noalias !204
  %5 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  store i8 0, ptr %5, align 8, !alias.scope !198, !noalias !206
  %_50.sroa.2.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 9
  store i8 %3, ptr %_50.sroa.2.0..sroa_idx.i.i, align 1, !alias.scope !198, !noalias !206
  %_50.sroa.3.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 10
  store i8 %_46.1.i.i, ptr %_50.sroa.3.0..sroa_idx.i.i, align 2, !alias.scope !198, !noalias !206
  br label %bb11.i

bb26.i.i:                                         ; preds = %start
  %6 = getelementptr inbounds nuw i8, ptr %c, i64 56
  %_7.i.i = load i64, ptr %6, align 8, !alias.scope !203, !noalias !204, !noundef !3
  %7 = getelementptr inbounds nuw i8, ptr %c, i64 96
  %_8.i.i = load i64, ptr %7, align 8, !alias.scope !203, !noalias !204, !noundef !3
  %_6.i.i = icmp eq i64 %_7.i.i, %_8.i.i
  br i1 %_6.i.i, label %bb2.i.i, label %bb11.i.i

bb2.i.i:                                          ; preds = %bb26.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_10.i.i), !noalias !207
  tail call void @llvm.experimental.noalias.scope.decl(metadata !208)
  %8 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 8) #17, !noalias !211
  %.sroa.6.0.extract.shift.i.i.i.i = lshr i64 %8, 32
  %9 = trunc i64 %8 to i1
  br i1 %9, label %bb6.i.i.i.i, label %bb7.i.i.i.i

bb6.i.i.i.i:                                      ; preds = %bb2.i.i
  %.sroa.4.0.extract.shift.i.i.i.i = lshr i64 %8, 8
  %.sroa.4.0.extract.trunc.i.i.i.i = trunc i64 %.sroa.4.0.extract.shift.i.i.i.i to i8
  br label %bb3.i.i.i

bb7.i.i.i.i:                                      ; preds = %bb2.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !217
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8.i.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 0) #17, !noalias !211
  %10 = load i8, ptr %_8.i.i.i.i, align 8, !range !118, !noalias !217, !noundef !3
  %11 = trunc nuw i8 %10 to i1
  br i1 %11, label %bb8.i.i.i.i, label %bb4.i.i.i

bb8.i.i.i.i:                                      ; preds = %bb7.i.i.i.i
  %12 = getelementptr inbounds nuw i8, ptr %_8.i.i.i.i, i64 1
  %_20.i.i.i.i = load i8, ptr %12, align 1, !range !218, !noalias !217, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !217
  br label %bb3.i.i.i

bb3.i.i.i:                                        ; preds = %bb8.i.i.i.i, %bb6.i.i.i.i
  %_4.sroa.5.0.ph.i.i.i = phi i8 [ %_20.i.i.i.i, %bb8.i.i.i.i ], [ %.sroa.4.0.extract.trunc.i.i.i.i, %bb6.i.i.i.i ]
  %switch.selectcmp.i.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i.i, 4
  %switch.select.i.i.i = select i1 %switch.selectcmp.i.i.i, i8 8, i8 2
  %switch.selectcmp8.i.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i.i, 3
  %switch.select9.i.i.i = select i1 %switch.selectcmp8.i.i.i, i8 6, i8 %switch.select.i.i.i
  %13 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %14 = load i8, ptr %13, align 2, !range !118, !alias.scope !219, !noalias !220, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select9.i.i.i, i8 noundef %14, i8 %switch.select9.i.i.i) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb4.i.i.i:                                        ; preds = %bb7.i.i.i.i
  %15 = getelementptr inbounds nuw i8, ptr %_8.i.i.i.i, i64 8
  %_19.i.i.i.i = load i64, ptr %15, align 8, !noalias !217, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !217
  %16 = icmp ult i64 %8, 281474976710656
  br i1 %16, label %bb2.i.i.i.i, label %bb6.i.i.i

bb2.i.i.i.i:                                      ; preds = %bb4.i.i.i
  %_5.i.i.i.i = lshr i64 %8, 40
  %trunc.i.i.i.i = trunc i64 %.sroa.6.0.extract.shift.i.i.i.i to i8
  switch i8 %trunc.i.i.i.i, label %bb6.i.i.i [
    i8 0, label %bb6.i11.i.i.i
    i8 1, label %bb5.i.i.i.i
    i8 2, label %bb4.i.i.i.i
  ]

bb6.i11.i.i.i:                                    ; preds = %bb2.i.i.i.i
  %17 = icmp samesign ult i64 %8, 1099511627776
  br i1 %17, label %bb7.i.i.i, label %bb6.i.i.i

bb5.i.i.i.i:                                      ; preds = %bb2.i.i.i.i
  %18 = icmp samesign ult i64 %8, 1099511627776
  br i1 %18, label %bb7.i.i.i, label %bb6.i.i.i

bb4.i.i.i.i:                                      ; preds = %bb2.i.i.i.i
  %_13.i.i.i.i = icmp samesign ult i64 %8, 10995116277760
  br i1 %_13.i.i.i.i, label %bb14.i.i.i.i, label %bb6.i.i.i

bb14.i.i.i.i:                                     ; preds = %bb4.i.i.i.i
  %19 = getelementptr inbounds nuw i8, ptr @alloc_cc193f9e0c79ce36721b88a4026725d4, i64 %_5.i.i.i.i
  %_14.i.i.i.i = load i8, ptr %19, align 1, !range !222, !noalias !223, !noundef !3
  br label %bb7.i.i.i

bb6.i.i.i:                                        ; preds = %bb4.i.i.i.i, %bb5.i.i.i.i, %bb6.i11.i.i.i, %bb2.i.i.i.i, %bb4.i.i.i
  %20 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %21 = load i8, ptr %20, align 2, !range !118, !alias.scope !219, !noalias !220, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %21, i8 3) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb7.i.i.i:                                        ; preds = %bb14.i.i.i.i, %bb5.i.i.i.i, %bb6.i11.i.i.i
  %_0.sroa.8.0.i.i.i.i = phi i8 [ %_14.i.i.i.i, %bb14.i.i.i.i ], [ 10, %bb6.i11.i.i.i ], [ 11, %bb5.i.i.i.i ]
  %_34.i.i.i = icmp ult i64 %_19.i.i.i.i, %_7.i.i
  br i1 %_34.i.i.i, label %bb33.i.i.i, label %bb21.i.i.i

bb21.i.i.i:                                       ; preds = %bb7.i.i.i
  %_20.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 88
  %_38.i.i.i = load i64, ptr %_20.i.i.i, align 8, !alias.scope !219, !noalias !220, !noundef !3
  %_39.i.i.i = icmp ugt i64 %_19.i.i.i.i, %_38.i.i.i
  br i1 %_39.i.i.i, label %bb33.i.i.i, label %bb24.i.i.i

bb24.i.i.i:                                       ; preds = %bb21.i.i.i
  %_44.i.i.i = icmp eq i64 %_7.i.i, -1
  br i1 %_44.i.i.i, label %bb34.i.i.i, label %bb26.i.i.i

bb26.i.i.i:                                       ; preds = %bb24.i.i.i
  %_22.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 32
  %_43.i.i.i = load i32, ptr %_22.i.i.i, align 8, !alias.scope !219, !noalias !220, !noundef !3
  %_42.i.i.i = zext i32 %_43.i.i.i to i64
  %_47.i.i.i = sub i64 %_19.i.i.i.i, %_7.i.i
  %_46.i.i.i = icmp ugt i64 %_47.i.i.i, %_42.i.i.i
  br i1 %_46.i.i.i, label %bb33.i.i.i, label %bb34.i.i.i

bb34.i.i.i:                                       ; preds = %bb26.i.i.i, %bb24.i.i.i
  store i64 %_19.i.i.i.i, ptr %7, align 8, !alias.scope !219, !noalias !220
  %22 = add nsw i8 %_0.sroa.8.0.i.i.i.i, -10
  %23 = icmp samesign ugt i8 %_0.sroa.8.0.i.i.i.i, 9
  %narrow.i.i.i = select i1 %23, i8 %22, i8 2
  switch i8 %narrow.i.i.i, label %bb2.i.i.i [
    i8 0, label %bb29.i.i
    i8 1, label %bb9.i.i.i
    i8 2, label %bb10.i.i.i
  ]

bb33.i.i.i:                                       ; preds = %bb26.i.i.i, %bb21.i.i.i, %bb7.i.i.i
  %checked.sroa.0.0.i.i.i = phi i8 [ 3, %bb26.i.i.i ], [ 4, %bb7.i.i.i ], [ 5, %bb21.i.i.i ]
  %24 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %25 = load i8, ptr %24, align 2, !range !118, !alias.scope !219, !noalias !220, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %checked.sroa.0.0.i.i.i, i8 noundef %25, i8 %checked.sroa.0.0.i.i.i) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb2.i.i.i:                                        ; preds = %bb34.i.i.i
  unreachable

bb9.i.i.i:                                        ; preds = %bb34.i.i.i
  %26 = getelementptr inbounds nuw i8, ptr %c, i64 104
  store i8 1, ptr %26, align 8, !alias.scope !219, !noalias !220
  br label %bb29.i.i

bb10.i.i.i:                                       ; preds = %bb34.i.i.i
  %27 = icmp samesign ult i8 %_0.sroa.8.0.i.i.i.i, 10
  tail call void @llvm.assume(i1 %27)
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %_0.sroa.8.0.i.i.i.i, i8 noundef 0, i8 %_0.sroa.8.0.i.i.i.i) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb10.i.i.i, %bb33.i.i.i, %bb6.i.i.i, %bb3.i.i.i
  %.pr.i.i = load i8, ptr %_10.i.i, align 8, !noalias !207
  %.not11.i.i = icmp eq i8 %.pr.i.i, 5
  br i1 %.not11.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i, label %bb28.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i: ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %_13.pre.i.i = load i64, ptr %6, align 8, !alias.scope !203, !noalias !204
  %_14.pre.i.i = load i64, ptr %7, align 8, !alias.scope !203, !noalias !204
  br label %bb29.i.i

bb11.i.i:                                         ; preds = %bb29.i.i, %bb26.i.i
  %28 = phi i64 [ %_13.i.i, %bb29.i.i ], [ %_7.i.i, %bb26.i.i ]
  %29 = getelementptr inbounds nuw i8, ptr %c, i64 80
  %off.i.i = load i64, ptr %29, align 8, !alias.scope !203, !noalias !204, !noundef !3
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %h.i.i), !noalias !207
  store i64 0, ptr %h.i.i, align 8, !noalias !207
  %30 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region4read(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef %off.i.i, ptr noalias noundef nonnull align 1 %h.i.i, i64 noundef 8) #17, !noalias !221
  %.not12.i.i = icmp eq i8 %30, 7
  br i1 %.not12.i.i, label %bb14.i.i, label %bb13.i.i

bb28.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %31 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %31, ptr noundef nonnull align 8 dereferenceable(16) %_10.i.i, i64 16, i1 false), !noalias !206
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_10.i.i), !noalias !207
  br label %bb11.i

bb29.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i, %bb9.i.i.i, %bb34.i.i.i
  %_14.i.i = phi i64 [ %_14.pre.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i ], [ %_19.i.i.i.i, %bb9.i.i.i ], [ %_19.i.i.i.i, %bb34.i.i.i ]
  %_13.i.i = phi i64 [ %_13.pre.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i ], [ %_7.i.i, %bb9.i.i.i ], [ %_7.i.i, %bb34.i.i.i ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_10.i.i), !noalias !207
  %_12.i.i = icmp eq i64 %_13.i.i, %_14.i.i
  br i1 %_12.i.i, label %bb5.i.i, label %bb11.i.i

bb5.i.i:                                          ; preds = %bb29.i.i
  %32 = getelementptr inbounds nuw i8, ptr %c, i64 104
  %33 = load i8, ptr %32, align 8, !range !118, !alias.scope !203, !noalias !204, !noundef !3
  %_15.i.i = trunc nuw i8 %33 to i1
  br i1 %_15.i.i, label %bb7.i.i, label %bb9.i.i

bb13.i.i:                                         ; preds = %bb11.i.i
  %switch.selectcmp.i.i = icmp eq i8 %30, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp14.i.i = icmp eq i8 %30, 3
  %switch.select15.i.i = select i1 %switch.selectcmp14.i.i, i8 6, i8 %switch.select.i.i
  %34 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %35 = load i8, ptr %34, align 2, !range !118, !alias.scope !203, !noalias !204, !noundef !3
  %36 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %36, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select15.i.i, i8 noundef %35, i8 %switch.select15.i.i) #17, !noalias !224
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !207
  br label %bb11.i

bb14.i.i:                                         ; preds = %bb11.i.i
  %_31.sroa.0.0.copyload.i.i = load i64, ptr %h.i.i, align 8, !noalias !207
  %.sroa.0.0.extract.trunc.i.i.i = trunc i64 %_31.sroa.0.0.copyload.i.i to i32
  %37 = icmp ugt i64 %_31.sroa.0.0.copyload.i.i, 1125899906842623
  %_21.i18.i.i = icmp ugt i32 %.sroa.0.0.extract.trunc.i.i.i, -8
  %or.cond.i.i = or i1 %37, %_21.i18.i.i
  br i1 %or.cond.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i, label %bb5.i.i.i

bb5.i.i.i:                                        ; preds = %bb14.i.i
  %38 = and i64 %_31.sroa.0.0.copyload.i.i, 281474976710656
  %39 = icmp eq i64 %38, 0
  %40 = icmp eq i32 %.sroa.0.0.extract.trunc.i.i.i, 16
  %or.cond.i20.i.i = or i1 %40, %39
  %.not38.i.i = icmp samesign ult i64 %_31.sroa.0.0.copyload.i.i, 281474976710656
  %or.cond39.i.i = and i1 %.not38.i.i, %or.cond.i20.i.i
  br i1 %or.cond39.i.i, label %bb18.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i: ; preds = %bb18.i.i, %bb5.i.i.i, %bb14.i.i
  %41 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %42 = load i8, ptr %41, align 2, !range !118, !alias.scope !203, !noalias !204, !noundef !3
  %43 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %43, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %42, i8 3) #17, !noalias !224
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !207
  br label %bb11.i

bb18.i.i:                                         ; preds = %bb5.i.i.i
  %_39.i.i = and i64 %_31.sroa.0.0.copyload.i.i, 4294967295
  %44 = getelementptr inbounds nuw i8, ptr %c, i64 36
  %_67.i.i = load i32, ptr %44, align 4, !alias.scope !203, !noalias !204, !noundef !3
  %_66.i.i = zext i32 %_67.i.i to i64
  %_41.i.i = add nsw i64 %_66.i.i, -8
  %_38.not.i.i = icmp ult i64 %_41.i.i, %_39.i.i
  br i1 %_38.not.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i, label %bb4.i

bb9.i.i:                                          ; preds = %bb5.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_19.i.i), !noalias !207
  %45 = getelementptr inbounds nuw i8, ptr %c, i64 64
  %_5.i.i.i = load i64, ptr %45, align 8, !alias.scope !225, !noalias !228, !noundef !3
  %_3.i.i.i = icmp eq i64 %_14.i.i, %_5.i.i.i
  br i1 %_3.i.i.i, label %bb3.i, label %bb2.i22.i.i

bb2.i22.i.i:                                      ; preds = %bb9.i.i
  %46 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 128, i64 noundef %_14.i.i) #17, !noalias !231
  %.not.i.i.i = icmp eq i8 %46, 7
  br i1 %.not.i.i.i, label %bb5.i27.i.i, label %bb4.i23.i.i

bb4.i23.i.i:                                      ; preds = %bb2.i22.i.i
  %switch.selectcmp.i24.i.i = icmp eq i8 %46, 4
  %switch.select.i25.i.i = select i1 %switch.selectcmp.i24.i.i, i8 8, i8 2
  %switch.selectcmp3.i.i.i = icmp eq i8 %46, 3
  %switch.select4.i.i.i = select i1 %switch.selectcmp3.i.i.i, i8 6, i8 %switch.select.i25.i.i
  %47 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %48 = load i8, ptr %47, align 2, !range !118, !alias.scope !225, !noalias !228, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_19.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select4.i.i.i, i8 noundef %48, i8 %switch.select4.i.i.i) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb5.i27.i.i:                                      ; preds = %bb2.i22.i.i
  store i64 %_14.i.i, ptr %45, align 8, !alias.scope !225, !noalias !228
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_19.i.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #17, !noalias !221
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i27.i.i, %bb4.i23.i.i
  %.pr36.i.i = load i8, ptr %_19.i.i, align 8, !noalias !207
  %.not.i.i = icmp eq i8 %.pr36.i.i, 5
  br i1 %.not.i.i, label %bb3.i, label %bb30.i.i

bb7.i.i:                                          ; preds = %bb5.i.i
  %49 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %49, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 11, i8 noundef 2, i8 undef) #17, !noalias !224
  br label %bb11.i

bb30.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %50 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %50, ptr noundef nonnull align 8 dereferenceable(16) %_19.i.i, i64 16, i1 false), !noalias !206
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_19.i.i), !noalias !207
  br label %bb11.i

bb11.i:                                           ; preds = %bb30.i.i, %bb7.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i, %bb13.i.i, %bb28.i.i, %bb27.i.i
  %51 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  %_29.sroa.0.0.copyload.i = load i64, ptr %51, align 8, !noalias !195
  %_29.sroa.5.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 16
  %_29.sroa.5.0.copyload.i = load i32, ptr %_29.sroa.5.0..sroa_idx.i, align 8, !noalias !195
  %_29.sroa.6.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 20
  %_29.sroa.6.0.copyload.i = load i16, ptr %_29.sroa.6.0..sroa_idx.i, align 4, !noalias !195
  %_29.sroa.7.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 22
  %_29.sroa.7.0.copyload.i = load i16, ptr %_29.sroa.7.0..sroa_idx.i, align 2, !noalias !195
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !195
  %52 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %_29.sroa.0.0.copyload.i, ptr %52, align 8, !alias.scope !190, !noalias !232
  %_31.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i32 %_29.sroa.5.0.copyload.i, ptr %_31.sroa.2.0..sroa_idx.i, align 8, !alias.scope !190, !noalias !232
  %_31.sroa.3.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 20
  store i16 %_29.sroa.6.0.copyload.i, ptr %_31.sroa.3.0..sroa_idx.i, align 4, !alias.scope !190, !noalias !232
  %_31.sroa.4.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 22
  store i16 %_29.sroa.7.0.copyload.i, ptr %_31.sroa.4.0..sroa_idx.i, align 2, !alias.scope !190, !noalias !232
  store i64 2, ptr %_0, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb18.i.i
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !207
  %53 = lshr i64 %_31.sroa.0.0.copyload.i.i, 32
  %54 = trunc nuw i64 %53 to i16
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !195
  %_32.not.i = icmp samesign ult i64 %_4.1, %_39.i.i
  br i1 %_32.not.i, label %bb15.i, label %bb13.i

bb3.i:                                            ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb9.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_19.i.i), !noalias !207
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !195
  store i64 0, ptr %_0, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb13.i:                                           ; preds = %bb4.i
  %_17.i = add i64 %off.i.i, 8
  %55 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region4read(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef %_17.i, ptr noalias noundef nonnull align 1 %_4.0, i64 noundef %_39.i.i) #17, !noalias !190
  %.not.i = icmp eq i8 %55, 7
  br i1 %.not.i, label %bb7.i, label %bb6.i

bb15.i:                                           ; preds = %bb4.i
  %56 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i8 3, ptr %56, align 8, !alias.scope !190, !noalias !232
  %_12.sroa.413.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i64 %_39.i.i, ptr %_12.sroa.413.0..sroa_idx.i, align 8, !alias.scope !190, !noalias !232
  store i64 2, ptr %_0, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb6.i:                                            ; preds = %bb13.i
  %switch.selectcmp.i = icmp eq i8 %55, 4
  %switch.select.i = select i1 %switch.selectcmp.i, i8 8, i8 2
  %switch.selectcmp17.i = icmp eq i8 %55, 3
  %switch.select18.i = select i1 %switch.selectcmp17.i, i8 6, i8 %switch.select.i
  %57 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %58 = load i8, ptr %57, align 2, !range !118, !alias.scope !193, !noalias !233, !noundef !3
  %59 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %59, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select18.i, i8 noundef %58, i8 %switch.select18.i) #17
  store i64 2, ptr %_0, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb7.i:                                            ; preds = %bb13.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_23.i), !noalias !195
  call void @llvm.experimental.noalias.scope.decl(metadata !234)
  %60 = add i64 %28, 1
  store i64 %60, ptr %6, align 8, !alias.scope !237, !noalias !238
  %61 = getelementptr inbounds nuw i8, ptr %c, i64 32
  %62 = add i64 %off.i.i, %_66.i.i
  %_28.i.i = load i32, ptr %61, align 8, !alias.scope !237, !noalias !238, !noundef !3
  %_27.i.i = zext i32 %_28.i.i to i64
  %_26.i.i = mul nuw i64 %_27.i.i, %_66.i.i
  %_25.i.i = add nuw i64 %_26.i.i, 512
  %_24.i.i = icmp eq i64 %62, %_25.i.i
  %spec.store.select.i.i = select i1 %_24.i.i, i64 512, i64 %62
  store i64 %spec.store.select.i.i, ptr %29, align 8, !alias.scope !237, !noalias !238
  %63 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 260) #17, !noalias !241
  %64 = trunc i64 %63 to i1
  br i1 %64, label %bb3.i.i, label %bb4.i.i

bb3.i.i:                                          ; preds = %bb7.i
  %.sroa.4.0.extract.shift.i.i = lshr i64 %63, 8
  %trunc.i.i = trunc i64 %.sroa.4.0.extract.shift.i.i to i8
  %switch.selectcmp.i37.i = icmp eq i8 %trunc.i.i, 4
  %switch.select.i38.i = select i1 %switch.selectcmp.i37.i, i8 8, i8 2
  %switch.selectcmp5.i.i = icmp eq i8 %trunc.i.i, 3
  %switch.select6.i.i = select i1 %switch.selectcmp5.i.i, i8 6, i8 %switch.select.i38.i
  %65 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %66 = load i8, ptr %65, align 2, !range !118, !alias.scope !237, !noalias !238, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_23.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select6.i.i, i8 noundef %66, i8 %switch.select6.i.i) #17, !noalias !190
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb7.i
  %67 = and i64 %63, 4294967296
  %producer_parked.i.i = icmp ne i64 %67, 0
  %68 = getelementptr inbounds nuw i8, ptr %c, i64 64
  %_17.i.i = load i64, ptr %68, align 8, !alias.scope !237, !noalias !238, !noundef !3
  %_15.i19.i = sub i64 %60, %_17.i.i
  %69 = getelementptr inbounds nuw i8, ptr %c, i64 72
  %_18.i.i = load i64, ptr %69, align 8, !alias.scope !237, !noalias !238, !noundef !3
  %_14.i20.i = icmp uge i64 %_15.i19.i, %_18.i.i
  %brmerge.i.i = or i1 %producer_parked.i.i, %_14.i20.i
  br i1 %brmerge.i.i, label %bb7.i22.i, label %bb24.i

bb7.i22.i:                                        ; preds = %bb4.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_20.i.i), !noalias !242
  %_3.i.i23.i = icmp eq i64 %60, %_17.i.i
  br i1 %_3.i.i23.i, label %bb20.i.i, label %bb2.i.i24.i

bb2.i.i24.i:                                      ; preds = %bb7.i22.i
  %70 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 128, i64 noundef %60) #17, !noalias !243
  %.not.i.i25.i = icmp eq i8 %70, 7
  br i1 %.not.i.i25.i, label %bb5.i.i35.i, label %bb4.i.i26.i

bb4.i.i26.i:                                      ; preds = %bb2.i.i24.i
  %switch.selectcmp.i.i27.i = icmp eq i8 %70, 4
  %switch.select.i.i28.i = select i1 %switch.selectcmp.i.i27.i, i8 8, i8 2
  %switch.selectcmp3.i.i29.i = icmp eq i8 %70, 3
  %switch.select4.i.i30.i = select i1 %switch.selectcmp3.i.i29.i, i8 6, i8 %switch.select.i.i28.i
  %71 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %72 = load i8, ptr %71, align 2, !range !118, !alias.scope !247, !noalias !249, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_20.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select4.i.i30.i, i8 noundef %72, i8 %switch.select4.i.i30.i) #17, !noalias !250
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i

bb5.i.i35.i:                                      ; preds = %bb2.i.i24.i
  store i64 %60, ptr %68, align 8, !alias.scope !247, !noalias !249
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_20.i.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #17, !noalias !250
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i: ; preds = %bb5.i.i35.i, %bb4.i.i26.i
  %.pr.i32.i = load i8, ptr %_20.i.i, align 8, !noalias !242
  %.not.i33.i = icmp eq i8 %.pr.i32.i, 5
  br i1 %.not.i33.i, label %bb20.i.i, label %bb19.i34.i

bb19.i34.i:                                       ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_23.i, ptr noundef nonnull align 8 dereferenceable(16) %_20.i.i, i64 16, i1 false), !noalias !251
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_20.i.i), !noalias !242
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb20.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i, %bb7.i22.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_20.i.i), !noalias !242
  br label %bb24.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb19.i34.i, %bb3.i.i
  %.pr.i = load i8, ptr %_23.i, align 8, !noalias !195
  %.not16.i = icmp eq i8 %.pr.i, 5
  br i1 %.not16.i, label %bb24.i, label %bb23.i

bb23.i:                                           ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  %73 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %73, ptr noundef nonnull align 8 dereferenceable(16) %_23.i, i64 16, i1 false), !noalias !232
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_23.i), !noalias !195
  store i64 2, ptr %_0, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb24.i:                                           ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb20.i.i, %bb4.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_23.i), !noalias !195
  store i64 1, ptr %_0, align 8, !alias.scope !190, !noalias !232
  %_25.sroa.4.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %_39.i.i, ptr %_25.sroa.4.0._0.sroa_idx.i, align 8, !alias.scope !190, !noalias !232
  %_25.sroa.5.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i16 %54, ptr %_25.sroa.5.0._0.sroa_idx.i, align 8, !alias.scope !190, !noalias !232
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb24.i, %bb23.i, %bb6.i, %bb15.i, %bb3.i, %bb11.i
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc noundef range(i8 0, 8) i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(40) %self, i1 noundef zeroext %up) unnamed_addr #1 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 38
  %1 = load i8, ptr %0, align 2, !range !118, !noundef !3
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 28
  %_8 = load i32, ptr %2, align 4, !noundef !3
  %_7 = and i32 %_8, 1
  %_6 = icmp ne i32 %_7, 0
  %_5 = xor i1 %up, %_6
  br i1 %_5, label %bb5, label %bb7

bb5:                                              ; preds = %start
  %_9 = add i32 %_8, 1
  store i32 %_9, ptr %2, align 4
  br label %bb7

bb7:                                              ; preds = %bb5, %start
  %_12 = phi i32 [ %_8, %start ], [ %_9, %bb5 ]
  %3 = trunc nuw i8 %1 to i1
  %. = select i1 %3, i64 388, i64 260
  %_0 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef %_12) #17
  ret i8 %_0
}

; Function Attrs: nounwind
declare void @llvm.x86.sse2.pause() unnamed_addr #17

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -7, 1) i64 @unir_consumer_sever(ptr noundef captures(address, read_provenance) %c, i32 noundef %cause_code) unnamed_addr #1 {
start:
  %err.i.i = alloca [16 x i8], align 8
  %_5.i = alloca [88 x i8], align 8
  %_10 = icmp ult i32 %cause_code, 10
  br i1 %_10, label %bb3, label %bb5

bb3:                                              ; preds = %start
  %_7 = zext nneg i32 %cause_code to i64
  %0 = getelementptr inbounds nuw i8, ptr @alloc_cc193f9e0c79ce36721b88a4026725d4, i64 %_7
  %_11 = load i8, ptr %0, align 1, !range !222, !noundef !3
  tail call void @llvm.experimental.noalias.scope.decl(metadata !252)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5.i), !noalias !252
  %1 = getelementptr inbounds nuw i8, ptr %_5.i, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_5.i, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false), !noalias !252
  store i64 65536, ptr %2, align 8, !noalias !252
  %3 = getelementptr inbounds nuw i8, ptr %_5.i, i64 64
  store i64 0, ptr %_5.i, align 8, !noalias !252
  %_12.sroa.4.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false), !noalias !252
  store ptr inttoptr (i64 8 to ptr), ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !noalias !252
  %_12.sroa.5.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 16
  store i64 0, ptr %_12.sroa.5.0._5.sroa_idx.i, align 8, !noalias !252
  tail call void @llvm.experimental.noalias.scope.decl(metadata !255)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i.i), !noalias !252
  %4 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %5 = load i8, ptr %4, align 4, !range !94, !alias.scope !258, !noalias !259, !noundef !3
  %.not.i.i = icmp eq i8 %5, 4
  br i1 %.not.i.i, label %bb8.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i: ; preds = %bb3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !252
  br label %bb6.i

bb8.i.i:                                          ; preds = %bb3
  %_14.i = getelementptr inbounds nuw i8, ptr %c, i64 16
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_14.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5.i, i8 noundef range(i8 0, 10) %_11, i8 noundef 1, i8 range(i8 0, 10) %_11) #17, !noalias !262
  %6 = load i8, ptr %err.i.i, align 8, !range !94, !noalias !263, !noundef !3
  %7 = icmp eq i8 %6, 0
  br i1 %7, label %bb4.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb8.i.i
  %8 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 1
  %9 = load i8, ptr %8, align 1, !range !100, !noalias !263, !noundef !3
  %10 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 2
  %_23.i.i = load i8, ptr %10, align 2, !range !222, !noalias !252
  %_22.i.i = icmp ne i8 %_23.i.i, %_11
  %_12.i.i = icmp ne i8 %9, 1
  %or.cond10.not.i = select i1 %_12.i.i, i1 true, i1 %_22.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !252
  %..i = sext i1 %or.cond10.not.i to i64
  br label %bb6.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb8.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !252
  %switch.tableidx = add nsw i8 %6, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6.i

bb6.i:                                            ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb4.i.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i
  %_0.sroa.0.0.i = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i ], [ %..i, %bb4.i.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i ]
  call void @llvm.experimental.noalias.scope.decl(metadata !264)
  call void @llvm.experimental.noalias.scope.decl(metadata !267)
  call void @llvm.experimental.noalias.scope.decl(metadata !270)
  call void @llvm.experimental.noalias.scope.decl(metadata !273)
  call void @llvm.experimental.noalias.scope.decl(metadata !276)
  %_9.i.i.i.i.i.i = load i64, ptr %_5.i, align 8, !range !116, !alias.scope !279, !noalias !252, !noundef !3
  %11 = icmp eq i64 %_9.i.i.i.i.i.i, 0
  br i1 %11, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit, label %bb6.i.i.i.i.i.i

bb6.i.i.i.i.i.i:                                  ; preds = %bb6.i
  %_10.i.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i.i, 24
  %_19.i.i.i.i.i.i = load ptr, ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !alias.scope !279, !noalias !252, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !279
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit

_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit: ; preds = %bb6.i.i.i.i.i.i, %bb6.i
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5.i), !noalias !252
  br label %bb5

bb5:                                              ; preds = %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit, %start
  %_0.sroa.0.0 = phi i64 [ %_0.sroa.0.0.i, %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit ], [ -7, %start ]
  ret i64 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable
define noundef range(i64 -5, -38654705143) i64 @unir_edge_len(i32 noundef %capacity, i32 noundef %slot_size) unnamed_addr #18 {
start:
  %.not.i.i = icmp ne i32 %capacity, 0
  %0 = icmp ugt i32 %slot_size, 8
  %or.cond.i.i = and i1 %.not.i.i, %0
  %1 = and i32 %slot_size, 7
  %2 = icmp eq i32 %1, 0
  %or.cond8.i.i = and i1 %or.cond.i.i, %2
  %_7 = zext i32 %capacity to i64
  %_8 = zext i32 %slot_size to i64
  %_6 = mul nuw i64 %_8, %_7
  %_5 = add nuw i64 %_6, 512
  %_0.sroa.0.0 = select i1 %or.cond8.i.i, i64 %_5, i64 -5
  ret i64 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable
define noundef range(i64 -8, 4294967288) i64 @unir_edge_max_payload(i32 noundef %capacity, i32 noundef %slot_size) unnamed_addr #18 {
start:
  %.not.i.i = icmp ne i32 %capacity, 0
  %0 = icmp ugt i32 %slot_size, 8
  %or.cond.i.i = and i1 %.not.i.i, %0
  %1 = and i32 %slot_size, 7
  %2 = icmp eq i32 %1, 0
  %or.cond8.i.i = and i1 %or.cond.i.i, %2
  %_6 = zext i32 %slot_size to i64
  %_5 = add nsw i64 %_6, -8
  %_0.sroa.0.0 = select i1 %or.cond8.i.i, i64 %_5, i64 -5
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -5, 1) i64 @unir_producer_complete(ptr noundef captures(address, read_provenance) %p) unnamed_addr #1 {
start:
  %err.i = alloca [16 x i8], align 8
  %_5 = alloca [88 x i8], align 8
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5)
  %0 = getelementptr inbounds nuw i8, ptr %_5, i64 24
  %1 = getelementptr inbounds nuw i8, ptr %_5, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %0, i8 0, i64 32, i1 false)
  store i64 65536, ptr %1, align 8
  %2 = getelementptr inbounds nuw i8, ptr %_5, i64 64
  store i64 0, ptr %_5, align 8
  %_11.sroa.4.0._5.sroa_idx = getelementptr inbounds nuw i8, ptr %_5, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %2, i8 0, i64 24, i1 false)
  store ptr inttoptr (i64 8 to ptr), ptr %_11.sroa.4.0._5.sroa_idx, align 8
  %_11.sroa.5.0._5.sroa_idx = getelementptr inbounds nuw i8, ptr %_5, i64 16
  store i64 0, ptr %_11.sroa.5.0._5.sroa_idx, align 8
  tail call void @llvm.experimental.noalias.scope.decl(metadata !280)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i)
  %3 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %4 = load i8, ptr %3, align 4, !range !94, !alias.scope !280, !noalias !283, !noundef !3
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb8.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3: ; preds = %start
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  br label %bb6

bb8.i:                                            ; preds = %start
  %_13 = getelementptr inbounds nuw i8, ptr %p, i64 32
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_13, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5, i8 noundef 11, i8 noundef 2, i8 undef) #17, !noalias !286
  %5 = load i8, ptr %err.i, align 8, !range !94, !noalias !287, !noundef !3
  %6 = icmp eq i8 %5, 0
  br i1 %6, label %bb4.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb8.i
  %7 = getelementptr inbounds nuw i8, ptr %err.i, i64 1
  %8 = load i8, ptr %7, align 1, !range !100, !noalias !287, !noundef !3
  %_21.i9 = icmp ne i8 %8, 2
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  %. = sext i1 %_21.i9 to i64
  br label %bb6

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb8.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  %switch.tableidx = add nsw i8 %5, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6

bb6:                                              ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, %bb4.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3
  %_0.sroa.0.0 = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3 ], [ %., %bb4.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit ]
  call void @llvm.experimental.noalias.scope.decl(metadata !288)
  call void @llvm.experimental.noalias.scope.decl(metadata !291)
  call void @llvm.experimental.noalias.scope.decl(metadata !294)
  call void @llvm.experimental.noalias.scope.decl(metadata !297)
  call void @llvm.experimental.noalias.scope.decl(metadata !300)
  %_9.i.i.i.i.i = load i64, ptr %_5, align 8, !range !116, !alias.scope !303, !noundef !3
  %9 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %9, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb6
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_11.sroa.4.0._5.sroa_idx, align 8, !alias.scope !303, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !303
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb6
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: read) uwtable
define noundef range(i32 0, 1024) i32 @unir_producer_ended(ptr noundef readonly captures(none) %p) unnamed_addr #15 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %1 = load i8, ptr %0, align 4, !range !94, !noundef !3
  %2 = getelementptr inbounds nuw i8, ptr %p, i64 69
  %3 = load i8, ptr %2, align 1
  %.not = icmp eq i8 %1, 4
  br i1 %.not, label %bb1, label %bb3

bb3:                                              ; preds = %start
  %4 = add nsw i8 %1, -2
  %.inv = icmp samesign ult i8 %1, 2
  %narrow = select i1 %.inv, i8 2, i8 %4
  switch i8 %narrow, label %bb2 [
    i8 0, label %bb1
    i8 1, label %bb5
    i8 2, label %bb4
  ]

bb1:                                              ; preds = %bb4, %bb5, %bb3, %start
  %_0.sroa.0.0 = phi i32 [ 2, %bb5 ], [ %8, %bb4 ], [ 0, %start ], [ 1, %bb3 ]
  ret i32 %_0.sroa.0.0

bb2:                                              ; preds = %bb3
  unreachable

bb5:                                              ; preds = %bb3
  br label %bb1

bb4:                                              ; preds = %bb3
  %_9 = zext i8 %3 to i32
  %_8 = shl nuw nsw i32 %_9, 2
  %5 = shl nuw i8 %1, 6
  %6 = and i8 %5, 64
  %7 = or disjoint i8 %6, 3
  %_7 = zext nneg i8 %7 to i32
  %8 = or i32 %_8, %_7
  br label %bb1
}

; Function Attrs: nounwind nonlazybind uwtable
define void @unir_producer_free(ptr noundef %p) unnamed_addr #1 {
start:
  %0 = icmp ne ptr %p, null
  tail call void @llvm.assume(i1 %0)
  tail call void @unir_host_free(ptr noundef nonnull %p, i64 noundef 72, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !304
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define noalias noundef ptr @unir_producer_open(ptr noundef %vat, i64 noundef %0, i32 noundef %1, i32 noundef %2) unnamed_addr #1 {
start:
  %_9.i = alloca [24 x i8], align 8
  %.not.i.i.i = icmp ne i32 %1, 0
  %3 = icmp ugt i32 %2, 8
  %or.cond.i.i.i = and i1 %.not.i.i.i, %3
  %4 = and i32 %2, 7
  %5 = icmp eq i32 %4, 0
  %or.cond8.i.i.i = and i1 %or.cond.i.i.i, %5
  br i1 %or.cond8.i.i.i, label %bb9.i, label %bb2

bb9.i:                                            ; preds = %start
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_9.i), !noalias !309
  %or.cond.i = icmp ugt i64 %0, 4294967295
  br i1 %or.cond.i, label %bb11.i, label %bb12.i

bb11.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !309
  br label %bb2

bb12.i:                                           ; preds = %bb9.i
  %6 = icmp ne ptr %vat, null
  tail call void @llvm.assume(i1 %6)
  %_25.i = trunc nuw i64 %0 to i32
  call void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr noalias noundef nonnull sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_9.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i32 noundef %_25.i) #17, !noalias !309
  %7 = load i8, ptr %_9.i, align 8, !range !118, !noalias !309, !noundef !3
  %8 = trunc nuw i8 %7 to i1
  br i1 %8, label %bb13.i, label %bb14.i

bb13.i:                                           ; preds = %bb12.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !309
  br label %bb2

bb14.i:                                           ; preds = %bb12.i
  %9 = getelementptr inbounds nuw i8, ptr %_9.i, i64 8
  %_29.0.i = load i64, ptr %9, align 8, !noalias !309, !noundef !3
  %10 = getelementptr inbounds nuw i8, ptr %_9.i, i64 16
  %_29.1.i = load i64, ptr %10, align 8, !noalias !309, !noundef !3
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !309
  %_12.i.i = zext i32 %1 to i64
  %_14.i.i = zext i32 %2 to i64
  %_11.i.i = mul nuw i64 %_14.i.i, %_12.i.i
  %_6.i.i = add nuw i64 %_11.i.i, 512
  %_3.i.i = icmp ult i64 %_29.1.i, %_6.i.i
  br i1 %_3.i.i, label %bb2, label %bb5

bb5:                                              ; preds = %bb14.i
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #17
  %_0.i.i.i = tail call noalias noundef ptr @unir_host_alloc(i64 noundef 72, i64 noundef range(i64 1, -9223372036854775807) 8) #17
  %11 = icmp eq ptr %_0.i.i.i, null
  br i1 %11, label %bb6, label %bb7, !prof !140

bb2:                                              ; preds = %bb7, %bb14.i, %bb13.i, %bb11.i, %start
  %_0.sroa.0.0 = phi ptr [ %_0.i.i.i, %bb7 ], [ null, %bb11.i ], [ null, %bb13.i ], [ null, %start ], [ null, %bb14.i ]
  ret ptr %_0.sroa.0.0

bb6:                                              ; preds = %bb5
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 72) #21
  unreachable

bb7:                                              ; preds = %bb5
  tail call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %_0.i.i.i, i8 0, i64 24, i1 false)
  %_14.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 24
  store i64 512, ptr %_14.sroa.4.0..sroa_idx, align 8
  %_14.sroa.5.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 32
  store i64 %_29.0.i, ptr %_14.sroa.5.0..sroa_idx, align 8
  %_14.sroa.6.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 40
  store i64 %_29.1.i, ptr %_14.sroa.6.0..sroa_idx, align 8
  %_14.sroa.7.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 48
  store i32 %1, ptr %_14.sroa.7.0..sroa_idx, align 8
  %_14.sroa.8.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 52
  store i32 %2, ptr %_14.sroa.8.0..sroa_idx, align 4
  %_14.sroa.9.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 56
  store i32 0, ptr %_14.sroa.9.0..sroa_idx, align 8
  %_14.sroa.10.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 60
  store i32 0, ptr %_14.sroa.10.0..sroa_idx, align 4
  %_14.sroa.11.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 64
  store i32 0, ptr %_14.sroa.11.0..sroa_idx, align 8
  %_14.sroa.12.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 68
  store i8 4, ptr %_14.sroa.12.0..sroa_idx, align 4
  %_14.sroa.14.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 70
  store i8 0, ptr %_14.sroa.14.0..sroa_idx, align 2
  br label %bb2
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -7, 1) i64 @unir_producer_sever(ptr noundef captures(address, read_provenance) %p, i32 noundef %cause_code) unnamed_addr #1 {
start:
  %err.i.i = alloca [16 x i8], align 8
  %_5.i = alloca [88 x i8], align 8
  %_10 = icmp ult i32 %cause_code, 10
  br i1 %_10, label %bb3, label %bb5

bb3:                                              ; preds = %start
  %_7 = zext nneg i32 %cause_code to i64
  %0 = getelementptr inbounds nuw i8, ptr @alloc_cc193f9e0c79ce36721b88a4026725d4, i64 %_7
  %_11 = load i8, ptr %0, align 1, !range !222, !noundef !3
  tail call void @llvm.experimental.noalias.scope.decl(metadata !313)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5.i), !noalias !313
  %1 = getelementptr inbounds nuw i8, ptr %_5.i, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_5.i, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false), !noalias !313
  store i64 65536, ptr %2, align 8, !noalias !313
  %3 = getelementptr inbounds nuw i8, ptr %_5.i, i64 64
  store i64 0, ptr %_5.i, align 8, !noalias !313
  %_12.sroa.4.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false), !noalias !313
  store ptr inttoptr (i64 8 to ptr), ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !noalias !313
  %_12.sroa.5.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 16
  store i64 0, ptr %_12.sroa.5.0._5.sroa_idx.i, align 8, !noalias !313
  tail call void @llvm.experimental.noalias.scope.decl(metadata !316)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i.i), !noalias !313
  %4 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %5 = load i8, ptr %4, align 4, !range !94, !alias.scope !319, !noalias !320, !noundef !3
  %.not.i.i = icmp eq i8 %5, 4
  br i1 %.not.i.i, label %bb8.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i: ; preds = %bb3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !313
  br label %bb6.i

bb8.i.i:                                          ; preds = %bb3
  %_14.i = getelementptr inbounds nuw i8, ptr %p, i64 32
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_14.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5.i, i8 noundef range(i8 0, 10) %_11, i8 noundef 0, i8 range(i8 0, 10) %_11) #17, !noalias !323
  %6 = load i8, ptr %err.i.i, align 8, !range !94, !noalias !324, !noundef !3
  %7 = icmp eq i8 %6, 0
  br i1 %7, label %bb4.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb8.i.i
  %8 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 1
  %9 = load i8, ptr %8, align 1, !range !100, !noalias !324, !noundef !3
  %10 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 2
  %_23.i.i = load i8, ptr %10, align 2, !range !222, !noalias !313
  %_22.i.i = icmp ne i8 %_23.i.i, %_11
  %11 = icmp ne i8 %9, 0
  %or.cond9.not.i = select i1 %11, i1 true, i1 %_22.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !313
  %..i = sext i1 %or.cond9.not.i to i64
  br label %bb6.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb8.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !313
  %switch.tableidx = add nsw i8 %6, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6.i

bb6.i:                                            ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb4.i.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i
  %_0.sroa.0.0.i = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i ], [ %..i, %bb4.i.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i ]
  call void @llvm.experimental.noalias.scope.decl(metadata !325)
  call void @llvm.experimental.noalias.scope.decl(metadata !328)
  call void @llvm.experimental.noalias.scope.decl(metadata !331)
  call void @llvm.experimental.noalias.scope.decl(metadata !334)
  call void @llvm.experimental.noalias.scope.decl(metadata !337)
  %_9.i.i.i.i.i.i = load i64, ptr %_5.i, align 8, !range !116, !alias.scope !340, !noalias !313, !noundef !3
  %12 = icmp eq i64 %_9.i.i.i.i.i.i, 0
  br i1 %12, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit, label %bb6.i.i.i.i.i.i

bb6.i.i.i.i.i.i:                                  ; preds = %bb6.i
  %_10.i.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i.i, 24
  %_19.i.i.i.i.i.i = load ptr, ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !alias.scope !340, !noalias !313, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !340
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit

_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit: ; preds = %bb6.i.i.i.i.i.i, %bb6.i
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5.i), !noalias !313
  br label %bb5

bb5:                                              ; preds = %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit, %start
  %_0.sroa.0.0 = phi i64 [ %_0.sroa.0.0.i, %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit ], [ -7, %start ]
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -5, 1) i64 @unir_producer_write(ptr noundef captures(address, read_provenance) %p, i16 noundef zeroext %substream, ptr noundef %buf, i64 noundef %len, i64 noundef %timeout_ns) unnamed_addr #1 {
start:
  %_38.i = alloca [16 x i8], align 8
  %_24.i = alloca [16 x i8], align 8
  %_7.i = alloca [16 x i8], align 8
  %_25 = alloca [24 x i8], align 8
  %_11 = alloca [2 x i8], align 2
  %_10 = alloca [88 x i8], align 8
  %_8 = alloca [16 x i8], align 8
  %0 = icmp eq i64 %len, 0
  %.buf = select i1 %0, ptr inttoptr (i64 1 to ptr), ptr %buf
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_8)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_10)
  %1 = getelementptr inbounds nuw i8, ptr %_10, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_10, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false)
  store i64 65536, ptr %2, align 8
  %3 = getelementptr inbounds nuw i8, ptr %_10, i64 64
  store i64 0, ptr %_10, align 8
  %_20.sroa.4.0._10.sroa_idx = getelementptr inbounds nuw i8, ptr %_10, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false)
  store ptr inttoptr (i64 8 to ptr), ptr %_20.sroa.4.0._10.sroa_idx, align 8
  %_20.sroa.5.0._10.sroa_idx = getelementptr inbounds nuw i8, ptr %_10, i64 16
  store i64 0, ptr %_20.sroa.5.0._10.sroa_idx, align 8
  call void @llvm.lifetime.start.p0(i64 2, ptr nonnull %_11)
  store i16 %substream, ptr %_11, align 2
  %_22 = icmp sgt i64 %timeout_ns, -1
  %_12.sroa.0.0 = zext i1 %_22 to i64
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_25)
  store ptr %_11, ptr %_25, align 8
  %4 = getelementptr inbounds nuw i8, ptr %_25, i64 8
  store ptr %.buf, ptr %4, align 8
  %5 = getelementptr inbounds nuw i8, ptr %_25, i64 16
  store i64 %len, ptr %5, align 8
  call void @llvm.experimental.noalias.scope.decl(metadata !341)
  %6 = getelementptr inbounds nuw i8, ptr %_7.i, i64 1
  %_0.i.i.i = getelementptr inbounds nuw i8, ptr %p, i64 32
  %7 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %8 = getelementptr inbounds nuw i8, ptr %p, i64 60
  %9 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  br label %bb26.i

bb27.i:                                           ; preds = %bb5.i
  %10 = load i8, ptr %7, align 2, !range !118, !alias.scope !344, !noalias !347, !noundef !3
  %_8.i.i = load i32, ptr %8, align 4, !alias.scope !344, !noalias !347, !noundef !3
  %_7.i.i = and i32 %_8.i.i, 1
  %_6.i.not.i = icmp eq i32 %_7.i.i, 0
  br i1 %_6.i.not.i, label %bb5.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

bb5.i.i:                                          ; preds = %bb27.i
  %_9.i.i = or disjoint i32 %_8.i.i, 1
  store i32 %_9.i.i, ptr %8, align 4, !alias.scope !344, !noalias !347
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb5.i.i, %bb27.i
  %_12.i.i = phi i32 [ %_8.i.i, %bb27.i ], [ %_9.i.i, %bb5.i.i ]
  %11 = trunc nuw i8 %10 to i1
  %..i.i = select i1 %11, i64 388, i64 260
  %_0.i.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i, i32 noundef %_12.i.i) #17, !noalias !351
  %.not.i = icmp eq i8 %_0.i.i, 7
  br i1 %.not.i, label %bb32.i, label %bb9.i

bb26.i:                                           ; preds = %bb26.i.backedge, %start
  %iter.sroa.0.047.i = phi i64 [ 0, %start ], [ %iter.sroa.0.047.i.be, %bb26.i.backedge ]
  %_42.i = add nuw nsw i64 %iter.sroa.0.047.i, 1
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_7.i), !noalias !352
  call fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(address) dereferenceable(16) %_7.i, ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %_25, ptr noalias noundef nonnull align 8 dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #22, !noalias !353
  %12 = load i8, ptr %_7.i, align 8, !range !138, !noalias !352, !noundef !3
  %.not19.i = icmp eq i8 %12, 5
  br i1 %.not19.i, label %bb29.i, label %bb28.i

bb32.i:                                           ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %..i = select i1 %11, i64 256, i64 384
  %13 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %_0.i.i.i, i64 noundef %..i) #17, !noalias !351
  %.sroa.6.0.extract.shift.i = lshr i64 %13, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %14 = trunc i64 %13 to i1
  br i1 %14, label %bb9.split.loop.exit48.i, label %bb10.i

bb9.split.loop.exit48.i:                          ; preds = %bb32.i
  %.sroa.4.0.extract.shift.le.i = lshr i64 %13, 8
  %.sroa.4.0.extract.trunc.le.i = trunc i64 %.sroa.4.0.extract.shift.le.i to i8
  br label %bb9.i

bb9.i:                                            ; preds = %bb9.split.loop.exit48.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %_16.sroa.6.0.i = phi i8 [ %.sroa.4.0.extract.trunc.le.i, %bb9.split.loop.exit48.i ], [ %_0.i.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i ]
  %switch.selectcmp.i = icmp eq i8 %_16.sroa.6.0.i, 4
  %switch.select.i = select i1 %switch.selectcmp.i, i8 8, i8 2
  %switch.selectcmp20.i = icmp eq i8 %_16.sroa.6.0.i, 3
  %switch.select21.i = select i1 %switch.selectcmp20.i, i8 6, i8 %switch.select.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select21.i, i8 noundef %10, i8 %switch.select21.i) #17, !noalias !354
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb10.i:                                           ; preds = %bb32.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_24.i), !noalias !352
  call fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(address) dereferenceable(16) %_24.i, ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %_25, ptr noalias noundef nonnull align 8 dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #22, !noalias !353
  %15 = load i8, ptr %_24.i, align 8, !range !138, !noalias !352, !noundef !3
  %.not16.i = icmp eq i8 %15, 5
  br i1 %.not16.i, label %bb43.i, label %bb42.i

bb42.i:                                           ; preds = %bb10.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_24.i), !noalias !352
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit

bb43.i:                                           ; preds = %bb10.i
  %16 = load i8, ptr %9, align 1, !range !118, !noalias !352, !noundef !3
  %_60.i = trunc nuw i8 %16 to i1
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_24.i), !noalias !352
  br i1 %_60.i, label %bb13.i, label %bb12.i

bb13.i:                                           ; preds = %bb43.i
  %17 = call fastcc noundef i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, i1 noundef zeroext false) #17, !noalias !351
  %.not18.i = icmp eq i8 %17, 7
  br i1 %.not18.i, label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread, label %bb16.i

bb12.i:                                           ; preds = %bb43.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_38.i), !noalias !352
  call void @llvm.experimental.noalias.scope.decl(metadata !355)
  call void @llvm.experimental.noalias.scope.decl(metadata !358)
  %18 = load i8, ptr %7, align 2, !range !118, !alias.scope !360, !noalias !361, !noundef !3
  %19 = trunc nuw i8 %18 to i1
  %..i28.i = select i1 %19, i64 256, i64 384
  %20 = call { i1, i8 } @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate4wait(ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i28.i, i32 noundef %.sroa.6.0.extract.trunc.i, i64 noundef range(i64 0, 2) %_12.sroa.0.0, i64 %timeout_ns) #17, !noalias !363
  %21 = extractvalue { i1, i8 } %20, 0
  %22 = extractvalue { i1, i8 } %20, 1
  br i1 %21, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb13.i.i

bb13.i.i:                                         ; preds = %bb12.i
  %_8.i.i.i = load i32, ptr %8, align 4, !alias.scope !364, !noalias !361, !noundef !3
  %_7.i.i.i = and i32 %_8.i.i.i, 1
  %_6.i.not.i.i = icmp eq i32 %_7.i.i.i, 0
  br i1 %_6.i.not.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, label %bb5.i.i.i

bb5.i.i.i:                                        ; preds = %bb13.i.i
  %_9.i.i.i = add i32 %_8.i.i.i, 1
  store i32 %_9.i.i.i, ptr %8, align 4, !alias.scope !364, !noalias !361
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i.i.i, %bb13.i.i
  %_12.i.i.i = phi i32 [ %_8.i.i.i, %bb13.i.i ], [ %_9.i.i.i, %bb5.i.i.i ]
  %..i.i.i = select i1 %19, i64 388, i64 260
  %_0.i.i29.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i.i, i32 noundef %_12.i.i.i) #17, !noalias !363
  %.not.not.i.i = icmp eq i8 %_0.i.i29.i, 7
  br i1 %.not.not.i.i, label %bb6.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %switch.i.i = icmp ult i8 %22, 2
  br i1 %switch.i.i, label %bb50.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i: ; preds = %bb6.i.i
  store i8 1, ptr %_38.i, align 8, !alias.scope !355, !noalias !367
  br label %bb49.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb12.i
  %waited.sroa.4.0.i.i = phi i8 [ %_0.i.i29.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i ], [ %22, %bb12.i ]
  %switch.selectcmp.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_38.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select8.i.i, i8 noundef %18, i8 %switch.select8.i.i) #17, !noalias !351
  %.pr.i = load i8, ptr %_38.i, align 8, !noalias !352
  %.not17.i = icmp eq i8 %.pr.i, 5
  br i1 %.not17.i, label %bb50.i, label %bb49.i

bb16.i:                                           ; preds = %bb13.i
  %switch.selectcmp22.i = icmp eq i8 %17, 4
  %switch.select23.i = select i1 %switch.selectcmp22.i, i8 8, i8 2
  %switch.selectcmp24.i = icmp eq i8 %17, 3
  %switch.select25.i = select i1 %switch.selectcmp24.i, i8 6, i8 %switch.select23.i
  %23 = load i8, ptr %7, align 2, !range !118, !alias.scope !341, !noalias !347, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select25.i, i8 noundef %23, i8 %switch.select25.i) #17, !noalias !354
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb49.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_8, ptr noundef nonnull align 8 dereferenceable(16) %_38.i, i64 16, i1 false), !noalias !368
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !352
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb50.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb6.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !352
  br label %bb26.i.backedge

bb28.i:                                           ; preds = %bb26.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_7.i), !noalias !352
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit

bb29.i:                                           ; preds = %bb26.i
  %24 = load i8, ptr %6, align 1, !range !118, !noalias !352, !noundef !3
  %_45.i = trunc nuw i8 %24 to i1
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_7.i), !noalias !352
  br i1 %_45.i, label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread, label %bb5.i

bb5.i:                                            ; preds = %bb29.i
  call void @llvm.x86.sse2.pause(), !noalias !351
  %exitcond.not.i = icmp eq i64 %_42.i, 64
  br i1 %exitcond.not.i, label %bb27.i, label %bb26.i.backedge

bb26.i.backedge:                                  ; preds = %bb5.i, %bb50.i
  %iter.sroa.0.047.i.be = phi i64 [ %_42.i, %bb5.i ], [ 0, %bb50.i ]
  br label %bb26.i

_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split: ; preds = %bb49.i, %bb16.i, %bb9.i
  %.pr = load i8, ptr %_8, align 8
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit

_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread: ; preds = %bb29.i, %bb13.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_25)
  call void @llvm.lifetime.end.p0(i64 2, ptr nonnull %_11)
  br label %bb12

_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split, %bb28.i, %bb42.i
  %25 = phi i8 [ %.pr, %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split ], [ %12, %bb28.i ], [ %15, %bb42.i ]
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_25)
  call void @llvm.lifetime.end.p0(i64 2, ptr nonnull %_11)
  %26 = zext i8 %25 to i64
  %switch.gep = getelementptr inbounds nuw [6 x i64], ptr @switch.table.unir_producer_write, i64 0, i64 %26
  %switch.load = load i64, ptr %switch.gep, align 8
  br label %bb12

bb12:                                             ; preds = %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit, %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread
  %_0.sroa.0.0 = phi i64 [ 0, %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread ], [ %switch.load, %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit ]
  call void @llvm.experimental.noalias.scope.decl(metadata !369)
  call void @llvm.experimental.noalias.scope.decl(metadata !372)
  call void @llvm.experimental.noalias.scope.decl(metadata !375)
  call void @llvm.experimental.noalias.scope.decl(metadata !378)
  call void @llvm.experimental.noalias.scope.decl(metadata !381)
  %_9.i.i.i.i.i = load i64, ptr %_10, align 8, !range !116, !alias.scope !384, !noundef !3
  %27 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %27, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb12
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_20.sroa.4.0._10.sroa_idx, align 8, !alias.scope !384, !nonnull !3, !noundef !3
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #17, !noalias !384
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb12
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_10)
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: inlinehint nounwind nonlazybind uwtable
define internal fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(24) %_1, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #16 {
start:
  %_12.i.i.i = alloca [16 x i8], align 8
  %_8.i.i.i = alloca [16 x i8], align 8
  %_71.i = alloca [1 x i8], align 1
  %_24.i = alloca [9 x i8], align 1
  %header2.i = alloca [8 x i8], align 8
  %header.i = alloca [8 x i8], align 4
  %_17.i = alloca [16 x i8], align 8
  %_6 = alloca [16 x i8], align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_6)
  %_10 = load ptr, ptr %_1, align 8, !nonnull !3, !align !385, !noundef !3
  %_7 = load i16, ptr %_10, align 2, !noundef !3
  %0 = getelementptr inbounds nuw i8, ptr %_1, i64 8
  %_11.0 = load ptr, ptr %0, align 8, !nonnull !3, !align !4, !noundef !3
  %1 = getelementptr inbounds nuw i8, ptr %_1, i64 16
  %_11.1 = load i64, ptr %1, align 8, !noundef !3
  tail call void @llvm.experimental.noalias.scope.decl(metadata !386)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !389)
  %2 = getelementptr inbounds nuw i8, ptr %p, i64 32
  %3 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %4 = load i8, ptr %3, align 4, !range !94, !alias.scope !389, !noalias !391, !noundef !3
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb20.i, label %bb21.i

bb21.i:                                           ; preds = %start
  %5 = getelementptr inbounds nuw i8, ptr %p, i64 69
  %_49.1.i = load i8, ptr %5, align 1, !alias.scope !389, !noalias !391
  %_53.sroa.3.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_6, i64 2
  store i8 %_49.1.i, ptr %_53.sroa.3.0._0.sroa_idx.i, align 2, !alias.scope !386, !noalias !394
  br label %bb4

bb20.i:                                           ; preds = %start
  %6 = getelementptr inbounds nuw i8, ptr %p, i64 48
  %7 = getelementptr inbounds nuw i8, ptr %p, i64 52
  %_55.i = load i32, ptr %7, align 4, !alias.scope !389, !noalias !391, !noundef !3
  %_54.i = zext i32 %_55.i to i64
  %_10.i = add nsw i64 %_54.i, -8
  %_8.i = icmp ugt i64 %_11.1, %_10.i
  br i1 %_8.i, label %bb4, label %bb3.i

bb3.i:                                            ; preds = %bb20.i
  %_13.i = load i64, ptr %p, align 8, !alias.scope !389, !noalias !391, !noundef !3
  %p.i = add i64 %_13.i, 1
  %8 = getelementptr inbounds nuw i8, ptr %p, i64 16
  %_56.i = load i64, ptr %8, align 8, !alias.scope !389, !noalias !391, !noundef !3
  %9 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %_57.i = load i64, ptr %9, align 8, !alias.scope !389, !noalias !391, !noundef !3
  %_59.i = load i32, ptr %6, align 8, !alias.scope !389, !noalias !391, !noundef !3
  %_58.i = zext i32 %_59.i to i64
  %10 = add i64 %_57.i, %_58.i
  %spec.store.select.i = tail call i64 @llvm.umin.i64(i64 %10, i64 %_56.i)
  %_14.i = icmp ugt i64 %p.i, %spec.store.select.i
  br i1 %_14.i, label %bb4.i, label %bb9.i

bb4.i:                                            ; preds = %bb3.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_17.i), !noalias !395
  tail call void @llvm.experimental.noalias.scope.decl(metadata !396)
  %11 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 144) #17, !noalias !399
  %.sroa.6.0.extract.shift.i.i.i = lshr i64 %11, 32
  %.sroa.6.0.extract.trunc.i.i.i = trunc nuw i64 %.sroa.6.0.extract.shift.i.i.i to i32
  %12 = trunc i64 %11 to i1
  br i1 %12, label %bb8.i.i.i, label %bb9.i.i.i

bb8.i.i.i:                                        ; preds = %bb4.i
  %.sroa.4.0.extract.shift.i.i.i = lshr i64 %11, 8
  %.sroa.4.0.extract.trunc.i.i.i = trunc i64 %.sroa.4.0.extract.shift.i.i.i to i8
  br label %bb3.i.i

bb9.i.i.i:                                        ; preds = %bb4.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !405
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 128) #17, !noalias !399
  %13 = load i8, ptr %_8.i.i.i, align 8, !range !118, !noalias !405, !noundef !3
  %14 = trunc nuw i8 %13 to i1
  br i1 %14, label %bb10.i.i.i, label %bb11.i.i.i

bb10.i.i.i:                                       ; preds = %bb9.i.i.i
  %15 = getelementptr inbounds nuw i8, ptr %_8.i.i.i, i64 1
  %_25.i.i.i = load i8, ptr %15, align 1, !range !218, !noalias !405, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !405
  br label %bb3.i.i

bb11.i.i.i:                                       ; preds = %bb9.i.i.i
  %16 = getelementptr inbounds nuw i8, ptr %_8.i.i.i, i64 8
  %_24.i.i.i = load i64, ptr %16, align 8, !noalias !405, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !405
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !405
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_12.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 136) #17, !noalias !399
  %17 = load i8, ptr %_12.i.i.i, align 8, !range !118, !noalias !405, !noundef !3
  %18 = trunc nuw i8 %17 to i1
  br i1 %18, label %bb12.i.i.i, label %bb4.i.i

bb12.i.i.i:                                       ; preds = %bb11.i.i.i
  %19 = getelementptr inbounds nuw i8, ptr %_12.i.i.i, i64 1
  %_30.i.i.i = load i8, ptr %19, align 1, !range !218, !noalias !405, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !405
  br label %bb3.i.i

bb3.i.i:                                          ; preds = %bb12.i.i.i, %bb10.i.i.i, %bb8.i.i.i
  %_4.sroa.5.0.ph.i.i = phi i8 [ %_30.i.i.i, %bb12.i.i.i ], [ %_25.i.i.i, %bb10.i.i.i ], [ %.sroa.4.0.extract.trunc.i.i.i, %bb8.i.i.i ]
  %switch.selectcmp.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  %20 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %21 = load i8, ptr %20, align 2, !range !118, !alias.scope !406, !noalias !407, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select8.i.i, i8 noundef %21, i8 %switch.select8.i.i) #17, !noalias !408
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb11.i.i.i
  %22 = getelementptr inbounds nuw i8, ptr %_12.i.i.i, i64 8
  %_29.i.i.i = load i64, ptr %22, align 8, !noalias !405, !noundef !3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !405
  %23 = tail call { i1, i8 } @_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal15decode_consumer(i32 noundef %.sroa.6.0.extract.trunc.i.i.i) #17, !noalias !407
  %24 = extractvalue { i1, i8 } %23, 0
  %25 = extractvalue { i1, i8 } %23, 1
  br i1 %24, label %bb9.i.i, label %bb6.i.i

bb9.i.i:                                          ; preds = %bb4.i.i
  %26 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %27 = load i8, ptr %26, align 2, !range !118, !alias.scope !406, !noalias !407, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %27, i8 3) #17, !noalias !408
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %bb4.i.i
  %28 = icmp ult i8 %25, 10
  %29 = icmp eq i8 %25, 12
  %30 = or i1 %28, %29
  br i1 %30, label %bb8.i.i, label %bb7.i.i

bb8.i.i:                                          ; preds = %bb6.i.i
  tail call void @llvm.assume(i1 %28)
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %25, i8 noundef 1, i8 %25) #17, !noalias !408
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb7.i.i:                                          ; preds = %bb6.i.i
  %_20.i.i = icmp eq i64 %_24.i.i.i, -1
  br i1 %_20.i.i, label %bb11.i.i, label %bb12.i.i

bb12.i.i:                                         ; preds = %bb7.i.i
  %_38.i.i = icmp ult i64 %_24.i.i.i, %_57.i
  br i1 %_38.i.i, label %bb34.i.i, label %bb25.i.i

bb11.i.i:                                         ; preds = %bb7.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 11, i8 noundef 3, i8 undef) #17, !noalias !408
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb25.i.i:                                         ; preds = %bb12.i.i
  %_44.i.i = icmp ugt i64 %_24.i.i.i, %_13.i
  %_46.i.i = sub i64 %_13.i, %_24.i.i.i
  %_45.i.i = icmp ugt i64 %_46.i.i, %_58.i
  %or.cond.i.i = or i1 %_44.i.i, %_45.i.i
  br i1 %or.cond.i.i, label %bb34.i.i, label %bb35.i.i

bb35.i.i:                                         ; preds = %bb25.i.i
  %_49.i.i = icmp ult i64 %_29.i.i.i, %_56.i
  br i1 %_49.i.i, label %bb34.i.i, label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i: ; preds = %bb35.i.i
  store i64 %_24.i.i.i, ptr %9, align 8, !alias.scope !406, !noalias !407
  store i64 %_29.i.i.i, ptr %8, align 8, !alias.scope !406, !noalias !407
  br label %bb26.i

bb34.i.i:                                         ; preds = %bb35.i.i, %bb25.i.i, %bb12.i.i
  %checked.sroa.0.0.i.i = phi i8 [ 4, %bb12.i.i ], [ 3, %bb25.i.i ], [ 4, %bb35.i.i ]
  %31 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %32 = load i8, ptr %31, align 2, !range !118, !alias.scope !406, !noalias !407, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %checked.sroa.0.0.i.i, i8 noundef %32, i8 %checked.sroa.0.0.i.i) #17, !noalias !408
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb34.i.i, %bb11.i.i, %bb8.i.i, %bb9.i.i, %bb3.i.i
  %.pr.i = load i8, ptr %_17.i, align 8, !noalias !395
  %.not16.i = icmp eq i8 %.pr.i, 5
  br i1 %.not16.i, label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i, label %bb25.i

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i: ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  %_64.pre.i = load i64, ptr %8, align 8, !alias.scope !389, !noalias !391
  %_65.pre.i = load i64, ptr %9, align 8, !alias.scope !389, !noalias !391
  %_67.pre.i = load i32, ptr %6, align 8, !alias.scope !389, !noalias !391
  %.pre.i = zext i32 %_67.pre.i to i64
  br label %bb26.i

bb9.i:                                            ; preds = %bb26.i, %bb3.i
  %_97.i = phi i32 [ %_67.i, %bb26.i ], [ %_59.i, %bb3.i ]
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %header.i), !noalias !395
  %_22.i = trunc i64 %_11.1 to i32
  store i32 %_22.i, ptr %header.i, align 4, !noalias !395
  %33 = getelementptr inbounds nuw i8, ptr %header.i, i64 4
  store i16 %_7, ptr %33, align 4, !noalias !395
  %34 = getelementptr inbounds nuw i8, ptr %header.i, i64 6
  store i16 0, ptr %34, align 2, !noalias !395
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %header2.i), !noalias !395
  call void @llvm.lifetime.start.p0(i64 9, ptr nonnull %_24.i), !noalias !395
  call void @_RNvMs0_CsdT7xHiCjqae_9unir_wireNtB5_11FrameHeader6encode(ptr noalias noundef nonnull sret([9 x i8]) align 1 captures(none) dereferenceable(9) %_24.i, ptr noalias noundef nonnull readonly align 4 captures(address, read_provenance) dereferenceable(8) %header.i) #17, !noalias !408
  %35 = load i8, ptr %_24.i, align 1, !range !118, !noalias !395, !noundef !3
  %36 = trunc nuw i8 %35 to i1
  br i1 %36, label %bb30.i, label %bb31.i, !prof !140

bb25.i:                                           ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_6, ptr noundef nonnull align 8 dereferenceable(16) %_17.i, i64 16, i1 false), !noalias !394
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_17.i), !noalias !395
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb26.i:                                           ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i
  %_66.pre-phi.i = phi i64 [ %.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_58.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_67.i = phi i32 [ %_67.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_59.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_65.i = phi i64 [ %_65.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_24.i.i.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_64.i = phi i64 [ %_64.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_29.i.i.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_17.i), !noalias !395
  %37 = add i64 %_65.i, %_66.pre-phi.i
  %spec.store.select4.i = tail call i64 @llvm.umin.i64(i64 %37, i64 %_64.i)
  %_19.i = icmp ugt i64 %p.i, %spec.store.select4.i
  br i1 %_19.i, label %bb5, label %bb9.i

bb30.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.start.p0(i64 1, ptr nonnull %_71.i), !noalias !395
  %38 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  %39 = load i8, ptr %38, align 1, !range !409, !noalias !395, !noundef !3
  store i8 %39, ptr %_71.i, align 1, !noalias !395
  call void @_RNvNtCscliFh4jUES5_4core6result13unwrap_failed(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) @alloc_27f7ec91e8c14f79b1c7bc4a57a83efa, i64 noundef 19, ptr noundef nonnull align 1 %_71.i, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(32) @vtable.0.73, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.74) #24, !noalias !408
  unreachable

bb31.i:                                           ; preds = %bb9.i
  %40 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  %41 = load i64, ptr %40, align 1, !noalias !395
  store i64 %41, ptr %header2.i, align 8, !noalias !395
  call void @llvm.lifetime.end.p0(i64 9, ptr nonnull %_24.i), !noalias !395
  %42 = getelementptr inbounds nuw i8, ptr %p, i64 24
  %43 = load i64, ptr %42, align 8, !alias.scope !389, !noalias !391, !noundef !3
  %_76.i = and i64 %_11.1, 7
  %44 = icmp eq i64 %_76.i, 0
  %45 = sub nuw nsw i64 8, %_76.i
  %46 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %43, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %header2.i, i64 noundef 8) #17, !noalias !408
  %.not17.i = icmp eq i8 %46, 7
  br i1 %.not17.i, label %bb37.i, label %bb17.i

bb37.i:                                           ; preds = %bb31.i
  %_81.i = add i64 %43, 8
  %47 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %_81.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_11.0, i64 noundef range(i64 0, -9223372036854775808) %_11.1) #17, !noalias !386
  %.not18.i = icmp eq i8 %47, 7
  br i1 %.not18.i, label %bb43.i, label %bb17.i

bb43.i:                                           ; preds = %bb37.i
  br i1 %44, label %bb46.i, label %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i

_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb43.i
  %_4.i.i = add nuw i64 %_11.1, 8
  %_3.i.i = add i64 %_4.i.i, %43
  %48 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %_3.i.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) @alloc_85fc59111fd0cef7ef4093da3840b035, i64 noundef %45) #17, !noalias !410
  %.not19.i = icmp eq i8 %48, 7
  br i1 %.not19.i, label %bb46.i, label %bb17.i

bb46.i:                                           ; preds = %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i, %bb43.i
  %49 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 0, i64 noundef %p.i) #17, !noalias !386
  %.not20.i = icmp eq i8 %49, 7
  br i1 %.not20.i, label %bb13.i, label %bb17.i

bb13.i:                                           ; preds = %bb46.i
  store i64 %p.i, ptr %p, align 8, !alias.scope !389, !noalias !391
  %_92.i = load i32, ptr %7, align 4, !alias.scope !389, !noalias !391, !noundef !3
  %_91.i = zext i32 %_92.i to i64
  %50 = add i64 %43, %_91.i
  %_96.i = zext i32 %_97.i to i64
  %_95.i = mul nuw i64 %_91.i, %_96.i
  %_94.i = add nuw i64 %_95.i, 512
  %_93.i = icmp eq i64 %50, %_94.i
  %spec.store.select5.i = select i1 %_93.i, i64 512, i64 %50
  store i64 %spec.store.select5.i, ptr %42, align 8, !alias.scope !389, !noalias !391
  %51 = call fastcc noundef i8 @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #17, !noalias !386
  %.not21.i = icmp eq i8 %51, 7
  br i1 %.not21.i, label %bb16.i, label %bb17.i

bb16.i:                                           ; preds = %bb13.i
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header2.i), !noalias !395
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header.i), !noalias !395
  br label %bb5

bb17.i:                                           ; preds = %bb13.i, %bb46.i, %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i, %bb37.i, %bb31.i
  %.sink11 = phi i8 [ %49, %bb46.i ], [ %48, %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i ], [ %47, %bb37.i ], [ %46, %bb31.i ], [ %51, %bb13.i ]
  %switch.selectcmp24.i = icmp eq i8 %.sink11, 4
  %switch.select25.i = select i1 %switch.selectcmp24.i, i8 8, i8 2
  %switch.selectcmp26.i = icmp eq i8 %.sink11, 3
  %switch.select27.i = select i1 %switch.selectcmp26.i, i8 6, i8 %switch.select25.i
  %52 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %53 = load i8, ptr %52, align 2, !range !118, !alias.scope !389, !noalias !391, !noundef !3
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_6, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select27.i, i8 noundef %53, i8 %switch.select27.i) #17
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header2.i), !noalias !395
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header.i), !noalias !395
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb17.i, %bb25.i
  %.pr = load i8, ptr %_6, align 8
  %.not = icmp eq i8 %.pr, 5
  %.phi.trans.insert = getelementptr inbounds nuw i8, ptr %_6, i64 1
  %.pre = load i8, ptr %.phi.trans.insert, align 1
  br i1 %.not, label %bb5, label %bb4

bb4:                                              ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, %bb20.i, %bb21.i
  %_14.sroa.5.0.copyload = phi i8 [ %.pre, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit ], [ %4, %bb20.i ], [ %4, %bb21.i ]
  %54 = phi i8 [ %.pr, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit ], [ 2, %bb20.i ], [ 0, %bb21.i ]
  %_14.sroa.6.0._6.sroa_idx = getelementptr inbounds nuw i8, ptr %_6, i64 2
  %_16.sroa.3.0._0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0, i64 2
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 2 dereferenceable(14) %_16.sroa.3.0._0.sroa_idx, ptr noundef nonnull align 2 dereferenceable(14) %_14.sroa.6.0._6.sroa_idx, i64 14, i1 false)
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_6)
  store i8 %54, ptr %_0, align 8
  %_16.sroa.2.0._0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 %_14.sroa.5.0.copyload, ptr %_16.sroa.2.0._0.sroa_idx, align 1
  br label %bb3

bb5:                                              ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, %bb16.i, %bb26.i
  %55 = phi i8 [ %.pre, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit ], [ 1, %bb16.i ], [ 0, %bb26.i ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_6)
  %56 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 %55, ptr %56, align 1
  store i8 5, ptr %_0, align 8
  br label %bb3

bb3:                                              ; preds = %bb5, %bb4
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc noundef range(i8 0, 8) i8 @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(40) %self, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #1 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 38
  %1 = load i8, ptr %0, align 2, !range !118, !noundef !3
  %2 = trunc nuw i8 %1 to i1
  %. = select i1 %2, i64 384, i64 256
  %.13 = select i1 %2, i64 260, i64 388
  %3 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %.13) #17
  %.sroa.6.0.extract.shift = lshr i64 %3, 32
  %.sroa.6.0.extract.trunc = trunc nuw i64 %.sroa.6.0.extract.shift to i32
  %4 = trunc i64 %3 to i1
  br i1 %4, label %bb16, label %bb17

bb16:                                             ; preds = %start
  %.sroa.4.0.extract.shift = lshr i64 %3, 8
  %.sroa.4.0.extract.trunc = trunc i64 %.sroa.4.0.extract.shift to i8
  br label %bb15

bb17:                                             ; preds = %start
  %_9 = and i32 %.sroa.6.0.extract.trunc, 1
  %5 = icmp ne i32 %_9, 0
  %6 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_11 = load i32, ptr %6, align 8
  %_10 = icmp ne i32 %_11, %.sroa.6.0.extract.trunc
  %or.cond = select i1 %5, i1 %_10, i1 false
  br i1 %or.cond, label %bb7, label %bb15

bb7:                                              ; preds = %bb17
  store i32 %.sroa.6.0.extract.trunc, ptr %6, align 8
  %7 = getelementptr inbounds nuw i8, ptr %self, i64 24
  %_13 = load i32, ptr %7, align 8, !noundef !3
  %_12 = add i32 %_13, 1
  store i32 %_12, ptr %7, align 8
  %8 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef %_12) #17
  %.not = icmp eq i8 %8, 7
  br i1 %.not, label %bb19, label %bb15

bb19:                                             ; preds = %bb7
  %9 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef 1) #17
  %10 = trunc i64 %9 to i1
  br i1 %10, label %bb20, label %bb15

bb20:                                             ; preds = %bb19
  %.sroa.410.0.extract.shift = lshr i64 %9, 8
  %.sroa.410.0.extract.trunc = trunc i64 %.sroa.410.0.extract.shift to i8
  br label %bb15

bb15:                                             ; preds = %bb20, %bb19, %bb7, %bb17, %bb16
  %_0.sroa.0.0 = phi i8 [ %.sroa.4.0.extract.trunc, %bb16 ], [ %.sroa.410.0.extract.trunc, %bb20 ], [ 7, %bb17 ], [ %8, %bb7 ], [ 7, %bb19 ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: inlinehint nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt(ptr noalias noundef readonly align 1 captures(none) dereferenceable(1) %self, ptr noalias noundef align 8 dereferenceable(24) %f) unnamed_addr #16 {
start:
  %0 = load i8, ptr %self, align 1, !range !409, !noundef !3
  %1 = zext nneg i8 %0 to i64
  %switch.gep = getelementptr inbounds nuw [9 x i64], ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt, i64 0, i64 %1
  %switch.load = load i64, ptr %switch.gep, align 8
  %2 = zext nneg i8 %0 to i64
  %reltable.shift = shl i64 %2, 2
  %reltable.intrinsic = call ptr @llvm.load.relative.i64(ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel, i64 %reltable.shift)
  %_0 = tail call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter9write_str(ptr noalias noundef nonnull align 8 dereferenceable(24) %f, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %reltable.intrinsic, i64 noundef %switch.load) #17
  ret i1 %_0
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -6, 4294967296) i64 @unir_region_create(ptr noundef nonnull %vat, i64 noundef %len) unnamed_addr #1 {
start:
  %0 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate13region_create(ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i64 noundef %len) #17
  %1 = trunc i64 %0 to i1
  %.sroa.5.0.extract.shift = lshr i64 %0, 32
  %_0.sroa.0.0 = select i1 %1, i64 -6, i64 %.sroa.5.0.extract.shift
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define noalias noundef nonnull ptr @unir_vat_root(i64 noundef %map_base, i64 noundef %map_end, i64 noundef %carve_base, i32 noundef %carve_log2, i32 noundef %0) unnamed_addr #1 {
start:
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #17
  %_0.i.i.i = tail call noalias noundef ptr @unir_host_alloc(i64 noundef 88, i64 noundef range(i64 1, -9223372036854775807) 8) #17
  %1 = icmp eq ptr %_0.i.i.i, null
  br i1 %1, label %bb5, label %bb6, !prof !140

bb5:                                              ; preds = %start
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 88) #21
  unreachable

bb6:                                              ; preds = %start
  %spec.store.select = tail call i32 @llvm.umax.i32(i32 %0, i32 1)
  %_14 = zext i32 %spec.store.select to i64
  %_13 = shl nuw nsw i64 %_14, 16
  %_12 = add i64 %_13, %map_base
  store i64 0, ptr %_0.i.i.i, align 8
  %_10.sroa.0.sroa.4.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 8
  store ptr inttoptr (i64 8 to ptr), ptr %_10.sroa.0.sroa.4.0._20.0.sroa_idx, align 8
  %_10.sroa.0.sroa.5.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 16
  store i64 0, ptr %_10.sroa.0.sroa.5.0._20.0.sroa_idx, align 8
  %_10.sroa.4.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 24
  store i64 %map_base, ptr %_10.sroa.4.0._20.0.sroa_idx, align 8
  %_10.sroa.5.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 32
  store i64 %map_end, ptr %_10.sroa.5.0._20.0.sroa_idx, align 8
  %_10.sroa.6.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 40
  store i64 %carve_base, ptr %_10.sroa.6.0._20.0.sroa_idx, align 8
  %_10.sroa.7.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 48
  store i32 %carve_log2, ptr %_10.sroa.7.0._20.0.sroa_idx, align 8
  %_10.sroa.8.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 52
  store i32 %0, ptr %_10.sroa.8.0._20.0.sroa_idx, align 4
  %_10.sroa.9.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 56
  store i64 %_12, ptr %_10.sroa.9.0._20.0.sroa_idx, align 8
  %_10.sroa.10.0._20.0.sroa_idx = getelementptr inbounds nuw i8, ptr %_0.i.i.i, i64 64
  tail call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %_10.sroa.10.0._20.0.sroa_idx, i8 -1, i64 24, i1 false)
  ret ptr %_0.i.i.i
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umax.i32(i32, i32) #5

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_region_call(i32 noundef, i32 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_region_map(i32 noundef, i64 noundef, i64 noundef, i64 noundef, i32 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region4read(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, ptr noalias noundef nonnull writeonly align 1 captures(address) %dst.0, i64 noundef range(i64 0, -9223372036854775808) %dst.1) unnamed_addr #1 {
start:
  %_58.0 = add i64 %dst.1, %off
  %_58.1 = icmp ult i64 %_58.0, %off
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_54 = load i64, ptr %0, align 8
  %_53 = icmp ugt i64 %_58.0, %_54
  %or.cond19 = select i1 %_58.1, i1 true, i1 %_53
  br i1 %or.cond19, label %bb6, label %bb12, !prof !413

bb12:                                             ; preds = %start
  %_56 = load i64, ptr %self, align 8, !noundef !3
  %_55 = add i64 %_56, %off
  %p.biased.i = add i64 %_55, 7
  %_4.sroa.0.0.i = and i64 %p.biased.i, -8
  %1 = sub i64 %_4.sroa.0.0.i, %_55
  %spec.store.select.i = tail call i64 @llvm.umin.i64(i64 range(i64 0, -9223372036854775808) %dst.1, i64 %1)
  %_6.i = sub nsw i64 %dst.1, %spec.store.select.i
  %words4.i = and i64 %_6.i, -8
  %_9.i = and i64 %_6.i, 7
  %_76 = getelementptr inbounds nuw i8, ptr %dst.0, i64 %spec.store.select.i
  %_8720 = icmp samesign eq i64 %spec.store.select.i, 0
  br i1 %_8720, label %bb17, label %bb18.preheader

bb18.preheader:                                   ; preds = %bb12
  %xtraiter = and i64 %spec.store.select.i, 3
  %2 = icmp samesign ult i64 %spec.store.select.i, 4
  br i1 %2, label %bb17.loopexit.unr-lcssa, label %bb18.preheader.new

bb18.preheader.new:                               ; preds = %bb18.preheader
  %unroll_iter = and i64 %spec.store.select.i, 9223372036854775804
  br label %bb18

bb18:                                             ; preds = %bb18, %bb18.preheader.new
  %iter.sroa.0.022 = phi ptr [ %dst.0, %bb18.preheader.new ], [ %_93.3, %bb18 ]
  %iter.sroa.5.021 = phi i64 [ 0, %bb18.preheader.new ], [ %_82.0.3, %bb18 ]
  %niter = phi i64 [ 0, %bb18.preheader.new ], [ %niter.next.3, %bb18 ]
  %_93 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.022, i64 1
  %_82.0 = or disjoint i64 %iter.sroa.5.021, 1
  %_21 = add i64 %iter.sroa.5.021, %_55
  %_20 = inttoptr i64 %_21 to ptr
  %3 = load volatile i8, ptr %_20, align 1
  store i8 %3, ptr %iter.sroa.0.022, align 1
  %_93.1 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.022, i64 2
  %_82.0.1 = or disjoint i64 %iter.sroa.5.021, 2
  %_21.1 = add i64 %_82.0, %_55
  %_20.1 = inttoptr i64 %_21.1 to ptr
  %4 = load volatile i8, ptr %_20.1, align 1
  store i8 %4, ptr %_93, align 1
  %_93.2 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.022, i64 3
  %_82.0.2 = or disjoint i64 %iter.sroa.5.021, 3
  %_21.2 = add i64 %_82.0.1, %_55
  %_20.2 = inttoptr i64 %_21.2 to ptr
  %5 = load volatile i8, ptr %_20.2, align 1
  store i8 %5, ptr %_93.1, align 1
  %_93.3 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.022, i64 4
  %_82.0.3 = add nuw nsw i64 %iter.sroa.5.021, 4
  %_21.3 = add i64 %_82.0.2, %_55
  %_20.3 = inttoptr i64 %_21.3 to ptr
  %6 = load volatile i8, ptr %_20.3, align 1
  store i8 %6, ptr %_93.2, align 1
  %niter.next.3 = add i64 %niter, 4
  %niter.ncmp.3 = icmp eq i64 %niter.next.3, %unroll_iter
  br i1 %niter.ncmp.3, label %bb17.loopexit.unr-lcssa, label %bb18

bb17.loopexit.unr-lcssa:                          ; preds = %bb18, %bb18.preheader
  %iter.sroa.0.022.unr = phi ptr [ %dst.0, %bb18.preheader ], [ %_93.3, %bb18 ]
  %iter.sroa.5.021.unr = phi i64 [ 0, %bb18.preheader ], [ %_82.0.3, %bb18 ]
  %lcmp.mod.not = icmp eq i64 %xtraiter, 0
  br i1 %lcmp.mod.not, label %bb17, label %bb18.epil

bb18.epil:                                        ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa
  %iter.sroa.0.022.epil = phi ptr [ %_93.epil, %bb18.epil ], [ %iter.sroa.0.022.unr, %bb17.loopexit.unr-lcssa ]
  %iter.sroa.5.021.epil = phi i64 [ %_82.0.epil, %bb18.epil ], [ %iter.sroa.5.021.unr, %bb17.loopexit.unr-lcssa ]
  %epil.iter = phi i64 [ %epil.iter.next, %bb18.epil ], [ 0, %bb17.loopexit.unr-lcssa ]
  %_93.epil = getelementptr inbounds nuw i8, ptr %iter.sroa.0.022.epil, i64 1
  %_82.0.epil = add nuw nsw i64 %iter.sroa.5.021.epil, 1
  %_21.epil = add i64 %iter.sroa.5.021.epil, %_55
  %_20.epil = inttoptr i64 %_21.epil to ptr
  %7 = load volatile i8, ptr %_20.epil, align 1
  store i8 %7, ptr %iter.sroa.0.022.epil, align 1
  %epil.iter.next = add i64 %epil.iter, 1
  %epil.iter.cmp.not = icmp eq i64 %epil.iter.next, %xtraiter
  br i1 %epil.iter.cmp.not, label %bb17, label %bb18.epil, !llvm.loop !414

bb17:                                             ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa, %bb12
  %t = add i64 %words4.i, %spec.store.select.i
  %_99 = icmp ult i64 %t, %spec.store.select.i
  %_95.not = icmp ugt i64 %t, %dst.1
  %or.cond = or i1 %_99, %_95.not
  br i1 %or.cond, label %bb21, label %bb20, !prof !416

bb21:                                             ; preds = %bb17
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %spec.store.select.i, i64 noundef %t, i64 noundef %dst.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #24
  unreachable

bb20:                                             ; preds = %bb17
  %_8.i = and i64 %_6.i, 9223372036854775800
  %_108.not23 = icmp eq i64 %_8.i, 0
  br i1 %_108.not23, label %bb26, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph

_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph: ; preds = %bb20
  %_32 = add i64 %spec.store.select.i, %_55
  %8 = add nsw i64 %_8.i, -8
  %9 = lshr exact i64 %8, 3
  %10 = add nuw nsw i64 %9, 1
  %xtraiter30 = and i64 %10, 3
  %11 = icmp ult i64 %8, 24
  br i1 %11, label %bb26.loopexit.unr-lcssa, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new

_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new: ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph
  %unroll_iter33 = and i64 %10, 4611686018427387900
  br label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit

bb26.loopexit.unr-lcssa:                          ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph
  %iter3.sroa.0.026.unr = phi i64 [ 0, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph ], [ %_105.0.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %iter2.sroa.0.024.unr = phi ptr [ %_76, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph ], [ %_113.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %lcmp.mod32.not = icmp eq i64 %xtraiter30, 0
  br i1 %lcmp.mod32.not, label %bb26, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil

_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil: ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil, %bb26.loopexit.unr-lcssa
  %iter3.sroa.0.026.epil = phi i64 [ %_105.0.epil, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil ], [ %iter3.sroa.0.026.unr, %bb26.loopexit.unr-lcssa ]
  %iter2.sroa.0.024.epil = phi ptr [ %_113.epil, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil ], [ %iter2.sroa.0.024.unr, %bb26.loopexit.unr-lcssa ]
  %epil.iter31 = phi i64 [ %epil.iter31.next, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil ], [ 0, %bb26.loopexit.unr-lcssa ]
  %_113.epil = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024.epil, i64 8
  %_105.0.epil = add nuw nsw i64 %iter3.sroa.0.026.epil, 1
  %_33.epil = shl i64 %iter3.sroa.0.026.epil, 3
  %_31.epil = add i64 %_32, %_33.epil
  %_30.epil = inttoptr i64 %_31.epil to ptr
  %12 = load volatile i64, ptr %_30.epil, align 8
  store i64 %12, ptr %iter2.sroa.0.024.epil, align 1, !alias.scope !417, !noalias !420
  %epil.iter31.next = add i64 %epil.iter31, 1
  %epil.iter31.cmp.not = icmp eq i64 %epil.iter31.next, %xtraiter30
  br i1 %epil.iter31.cmp.not, label %bb26, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil, !llvm.loop !422

bb26:                                             ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil, %bb26.loopexit.unr-lcssa, %bb20
  %_41 = add nuw i64 %t, %_9.i
  %_119.not = icmp ugt i64 %_41, %dst.1
  br i1 %_119.not, label %bb29, label %bb28, !prof !416

_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit: ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new
  %iter3.sroa.0.026 = phi i64 [ 0, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %_105.0.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %iter2.sroa.0.024 = phi ptr [ %_76, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %_113.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %niter34 = phi i64 [ 0, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %niter34.next.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %_113 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 8
  %_33 = shl i64 %iter3.sroa.0.026, 3
  %_31 = add i64 %_32, %_33
  %_30 = inttoptr i64 %_31 to ptr
  %13 = load volatile i64, ptr %_30, align 8
  store i64 %13, ptr %iter2.sroa.0.024, align 1, !alias.scope !417, !noalias !420
  %_113.1 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 16
  %_105.0 = shl i64 %iter3.sroa.0.026, 3
  %_33.1 = or disjoint i64 %_105.0, 8
  %_31.1 = add i64 %_32, %_33.1
  %_30.1 = inttoptr i64 %_31.1 to ptr
  %14 = load volatile i64, ptr %_30.1, align 8
  store i64 %14, ptr %_113, align 1, !alias.scope !417, !noalias !420
  %_113.2 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 24
  %_105.0.1 = shl i64 %iter3.sroa.0.026, 3
  %_33.2 = or disjoint i64 %_105.0.1, 16
  %_31.2 = add i64 %_32, %_33.2
  %_30.2 = inttoptr i64 %_31.2 to ptr
  %15 = load volatile i64, ptr %_30.2, align 8
  store i64 %15, ptr %_113.1, align 1, !alias.scope !417, !noalias !420
  %_113.3 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 32
  %_105.0.3 = add nuw nsw i64 %iter3.sroa.0.026, 4
  %_105.0.2 = shl i64 %iter3.sroa.0.026, 3
  %_33.3 = or disjoint i64 %_105.0.2, 24
  %_31.3 = add i64 %_32, %_33.3
  %_30.3 = inttoptr i64 %_31.3 to ptr
  %16 = load volatile i64, ptr %_30.3, align 8
  store i64 %16, ptr %_113.2, align 1, !alias.scope !417, !noalias !420
  %niter34.next.3 = add i64 %niter34, 4
  %niter34.ncmp.3 = icmp eq i64 %niter34.next.3, %unroll_iter33
  br i1 %niter34.ncmp.3, label %bb26.loopexit.unr-lcssa, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit

bb29:                                             ; preds = %bb26
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %t, i64 noundef %_41, i64 noundef %dst.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #24
  unreachable

bb28:                                             ; preds = %bb26
  %_126 = getelementptr inbounds nuw i8, ptr %dst.0, i64 %t
  %_13927 = icmp samesign eq i64 %_9.i, 0
  br i1 %_13927, label %bb6, label %bb35.lr.ph

bb35.lr.ph:                                       ; preds = %bb28
  %_48 = add i64 %t, %_55
  %_46 = inttoptr i64 %_48 to ptr
  %17 = load volatile i8, ptr %_46, align 1
  store i8 %17, ptr %_126, align 1
  %_139 = icmp samesign eq i64 %_9.i, 1
  br i1 %_139, label %bb6, label %bb35.1

bb35.1:                                           ; preds = %bb35.lr.ph
  %_145 = getelementptr inbounds nuw i8, ptr %_126, i64 1
  %_47.1 = add i64 %_48, 1
  %_46.1 = inttoptr i64 %_47.1 to ptr
  %18 = load volatile i8, ptr %_46.1, align 1
  store i8 %18, ptr %_145, align 1
  %_139.1 = icmp samesign eq i64 %_9.i, 2
  br i1 %_139.1, label %bb6, label %bb35.2

bb35.2:                                           ; preds = %bb35.1
  %_145.1 = getelementptr inbounds nuw i8, ptr %_126, i64 2
  %_47.2 = add i64 %_48, 2
  %_46.2 = inttoptr i64 %_47.2 to ptr
  %19 = load volatile i8, ptr %_46.2, align 1
  store i8 %19, ptr %_145.1, align 1
  %_139.2 = icmp samesign eq i64 %_9.i, 3
  br i1 %_139.2, label %bb6, label %bb35.3

bb35.3:                                           ; preds = %bb35.2
  %_145.2 = getelementptr inbounds nuw i8, ptr %_126, i64 3
  %_47.3 = add i64 %_48, 3
  %_46.3 = inttoptr i64 %_47.3 to ptr
  %20 = load volatile i8, ptr %_46.3, align 1
  store i8 %20, ptr %_145.2, align 1
  %_139.3 = icmp samesign eq i64 %_9.i, 4
  br i1 %_139.3, label %bb6, label %bb35.4

bb35.4:                                           ; preds = %bb35.3
  %_145.3 = getelementptr inbounds nuw i8, ptr %_126, i64 4
  %_47.4 = add i64 %_48, 4
  %_46.4 = inttoptr i64 %_47.4 to ptr
  %21 = load volatile i8, ptr %_46.4, align 1
  store i8 %21, ptr %_145.3, align 1
  %_139.4 = icmp samesign eq i64 %_9.i, 5
  br i1 %_139.4, label %bb6, label %bb35.5

bb35.5:                                           ; preds = %bb35.4
  %_145.4 = getelementptr inbounds nuw i8, ptr %_126, i64 5
  %_47.5 = add i64 %_48, 5
  %_46.5 = inttoptr i64 %_47.5 to ptr
  %22 = load volatile i8, ptr %_46.5, align 1
  store i8 %22, ptr %_145.4, align 1
  %_139.5 = icmp samesign eq i64 %_9.i, 6
  br i1 %_139.5, label %bb6, label %bb35.6

bb35.6:                                           ; preds = %bb35.5
  %_145.5 = getelementptr inbounds nuw i8, ptr %_126, i64 6
  %_47.6 = add i64 %_48, 6
  %_46.6 = inttoptr i64 %_47.6 to ptr
  %23 = load volatile i8, ptr %_46.6, align 1
  store i8 %23, ptr %_145.5, align 1
  br label %bb6

bb6:                                              ; preds = %bb35.6, %bb35.5, %bb35.4, %bb35.3, %bb35.2, %bb35.1, %bb35.lr.ph, %bb28, %start
  %_0.sroa.0.0 = phi i8 [ 2, %start ], [ 7, %bb28 ], [ 7, %bb35.6 ], [ 7, %bb35.5 ], [ 7, %bb35.4 ], [ 7, %bb35.3 ], [ 7, %bb35.2 ], [ 7, %bb35.1 ], [ 7, %bb35.lr.ph ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define internal void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr dead_on_unwind noalias noundef writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) initializes((0, 1)) %_0, ptr noalias noundef align 8 captures(none) dereferenceable(88) %self, i32 noundef %region) unnamed_addr #1 {
start:
  %_25 = icmp slt i32 %region, 0
  br i1 %_25, label %bb10, label %bb11

bb11:                                             ; preds = %start
  %n.i = tail call noundef i64 @__vm_region_call(i32 noundef %region, i32 noundef 2, i64 noundef 0, i64 noundef 0, i64 noundef 0, i64 noundef 0) #17, !noalias !423
  %_4.i = icmp sgt i64 %n.i, -1
  br i1 %_4.i, label %bb13, label %bb12

bb10:                                             ; preds = %start
  %0 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 1, ptr %0, align 1
  br label %bb9

bb12:                                             ; preds = %bb11
  %1 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 1, ptr %1, align 1
  br label %bb9

bb13:                                             ; preds = %bb11
  %_36 = and i64 %n.i, 65535
  %2 = icmp eq i64 %_36, 0
  %reass.sub = and i64 %n.i, 9223372036854710272
  %_39.0 = add nuw i64 %reass.sub, 65536
  %span.sroa.0.0 = select i1 %2, i64 %n.i, i64 %_39.0
  %3 = getelementptr inbounds nuw i8, ptr %self, i64 56
  %_15 = load i64, ptr %3, align 8, !noundef !3
  %_14 = add i64 %_15, %span.sroa.0.0
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_17 = load i64, ptr %4, align 8, !noundef !3
  %_13 = icmp ugt i64 %_14, %_17
  br i1 %_13, label %bb3, label %bb4

bb4:                                              ; preds = %bb13
  %r.i = tail call noundef i64 @__vm_region_map(i32 noundef %region, i64 noundef %_15, i64 noundef 0, i64 noundef %span.sroa.0.0, i32 noundef 3) #17
  %_7.i = icmp sgt i64 %r.i, -1
  br i1 %_7.i, label %bb20, label %bb19

bb3:                                              ; preds = %bb13
  %5 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 2, ptr %5, align 1
  br label %bb9

bb19:                                             ; preds = %bb4
  %6 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 2, ptr %6, align 1
  br label %bb9

bb20:                                             ; preds = %bb4
  store i64 %_14, ptr %3, align 8
  %7 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %_15, ptr %7, align 8
  %8 = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i64 %n.i, ptr %8, align 8
  br label %bb9

bb9:                                              ; preds = %bb20, %bb19, %bb3, %bb12, %bb10
  %.sink = phi i8 [ 1, %bb10 ], [ 1, %bb19 ], [ 1, %bb3 ], [ 1, %bb12 ], [ 0, %bb20 ]
  store i8 %.sink, ptr %_0, align 8
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal range(i64 0, 9223372032559808514) i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate13region_create(ptr noalias noundef align 8 captures(none) dereferenceable(88) %self, i64 noundef %len) unnamed_addr #1 {
start:
  %_17 = and i64 %len, 65535
  %0 = icmp eq i64 %_17, 0
  br i1 %0, label %bb14, label %bb8

bb8:                                              ; preds = %start
  %reass.sub = and i64 %len, -65536
  %_19.0 = add i64 %reass.sub, 65536
  %_19.1 = icmp ult i64 %_19.0, %len
  br i1 %_19.1, label %bb7, label %bb14, !prof !140

bb14:                                             ; preds = %bb8, %start
  %_5.sroa.5.0 = phi i64 [ %len, %start ], [ %_19.0, %bb8 ]
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 72
  %_10 = load i64, ptr %1, align 8, !noundef !3
  %_8 = icmp ugt i64 %_5.sroa.5.0, %_10
  br i1 %_8, label %bb7, label %bb2

bb2:                                              ; preds = %bb14
  %2 = tail call noundef i64 @__vm_region_create(i64 noundef %_5.sroa.5.0) #17
  %_0.i.i = icmp ult i64 %2, 2147483648
  br i1 %_0.i.i, label %bb4, label %bb7

bb4:                                              ; preds = %bb2
  %3 = sub i64 %_10, %_5.sroa.5.0
  store i64 %3, ptr %1, align 8
  %4 = shl nuw nsw i64 %2, 32
  br label %bb7

bb7:                                              ; preds = %bb4, %bb2, %bb14, %bb8
  %_0.sroa.0.0 = phi i64 [ 0, %bb4 ], [ 1, %bb8 ], [ 1, %bb14 ], [ 1, %bb2 ]
  %_0.sroa.5.0.insert.insert = phi i64 [ %4, %bb4 ], [ 512, %bb8 ], [ 1024, %bb14 ], [ 1024, %bb2 ]
  %_0.sroa.0.0.insert.insert = or disjoint i64 %_0.sroa.5.0.insert.insert, %_0.sroa.0.0
  ret i64 %_0.sroa.0.0.insert.insert
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_region_create(i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
define internal { i1, i8 } @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate4wait(ptr noalias noundef readnone align 8 captures(none) dereferenceable(88) %self, ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %region, i64 noundef %off, i32 noundef %expected, i64 noundef range(i64 0, 2) %0, i64 %1) unnamed_addr #1 {
start:
  %_5.i = and i64 %off, 3
  %2 = icmp eq i64 %_5.i, 0
  br i1 %2, label %bb1.i, label %bb7

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 4
  %_16.1.i = icmp ugt i64 %off, -5
  %3 = getelementptr inbounds nuw i8, ptr %region, i64 8
  %_12.i = load i64, ptr %3, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb7, label %bb9, !prof !413

bb9:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %region, align 8, !alias.scope !426, !noalias !429, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %4 = trunc nuw i64 %0 to i1
  br i1 %4, label %bb11, label %bb10

bb11:                                             ; preds = %bb9
  %spec.store.select = tail call i64 @llvm.umin.i64(i64 %1, i64 9223372036854775807)
  %5 = tail call noundef i32 @__vm_wait32(i64 noundef %_13.i, i32 noundef %expected, i64 noundef %spec.store.select) #17
  %6 = icmp ugt i32 %5, 2
  %switch.idx.cast = trunc i32 %5 to i8
  %spec.select = select i1 %6, i8 6, i8 %switch.idx.cast
  br label %bb7

bb10:                                             ; preds = %bb9
  %7 = tail call noundef i32 @__vm_wait32(i64 noundef %_13.i, i32 noundef %expected, i64 noundef 1000000000) #17
  %8 = icmp ult i32 %7, 3
  br i1 %8, label %switch.lookup13, label %bb7

switch.lookup13:                                  ; preds = %bb10
  %switch.cast = trunc nuw i32 %7 to i24
  %switch.shiftamt = shl nuw nsw i24 %switch.cast, 3
  %switch.downshift = lshr i24 256, %switch.shiftamt
  %switch.masked = trunc i24 %switch.downshift to i8
  br label %bb7

bb7:                                              ; preds = %switch.lookup13, %bb10, %bb11, %bb1.i, %start
  %_0.sroa.7.1 = phi i8 [ 6, %bb10 ], [ 2, %start ], [ 2, %bb1.i ], [ %switch.masked, %switch.lookup13 ], [ %spec.select, %bb11 ]
  %_0.sroa.0.1 = phi i1 [ true, %bb10 ], [ true, %start ], [ true, %bb1.i ], [ false, %switch.lookup13 ], [ %6, %bb11 ]
  %9 = insertvalue { i1, i8 } poison, i1 %_0.sroa.0.1, 0
  %10 = insertvalue { i1, i8 } %9, i8 %_0.sroa.7.1, 1
  ret { i1, i8 } %10
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i32 @__vm_wait32(i64 noundef, i32 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, ptr noalias noundef nonnull readonly align 1 captures(address) %src.0, i64 noundef range(i64 0, -9223372036854775808) %src.1) unnamed_addr #1 {
start:
  %_54.0 = add i64 %src.1, %off
  %_54.1 = icmp ult i64 %_54.0, %off
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_50 = load i64, ptr %0, align 8
  %_49 = icmp ugt i64 %_54.0, %_50
  %or.cond17 = select i1 %_54.1, i1 true, i1 %_49
  br i1 %or.cond17, label %bb6, label %bb12, !prof !413

bb12:                                             ; preds = %start
  %_52 = load i64, ptr %self, align 8, !noundef !3
  %_51 = add i64 %_52, %off
  %p.biased.i = add i64 %_51, 7
  %_4.sroa.0.0.i = and i64 %p.biased.i, -8
  %1 = sub i64 %_4.sroa.0.0.i, %_51
  %spec.store.select.i = tail call i64 @llvm.umin.i64(i64 range(i64 0, -9223372036854775808) %src.1, i64 %1)
  %_6.i = sub nsw i64 %src.1, %spec.store.select.i
  %words4.i = and i64 %_6.i, -8
  %_9.i = and i64 %_6.i, 7
  %_73 = getelementptr inbounds nuw i8, ptr %src.0, i64 %spec.store.select.i
  %_8418 = icmp samesign eq i64 %spec.store.select.i, 0
  br i1 %_8418, label %bb17, label %bb18.preheader

bb18.preheader:                                   ; preds = %bb12
  %xtraiter = and i64 %spec.store.select.i, 3
  %2 = icmp samesign ult i64 %spec.store.select.i, 4
  br i1 %2, label %bb17.loopexit.unr-lcssa, label %bb18.preheader.new

bb18.preheader.new:                               ; preds = %bb18.preheader
  %unroll_iter = and i64 %spec.store.select.i, 9223372036854775804
  br label %bb18

bb18:                                             ; preds = %bb18, %bb18.preheader.new
  %iter.sroa.0.020 = phi ptr [ %src.0, %bb18.preheader.new ], [ %_90.3, %bb18 ]
  %iter.sroa.5.019 = phi i64 [ 0, %bb18.preheader.new ], [ %_79.0.3, %bb18 ]
  %niter = phi i64 [ 0, %bb18.preheader.new ], [ %niter.next.3, %bb18 ]
  %_90 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 1
  %_79.0 = or disjoint i64 %iter.sroa.5.019, 1
  %b = load i8, ptr %iter.sroa.0.020, align 1, !noundef !3
  %_19 = add i64 %iter.sroa.5.019, %_51
  %_18 = inttoptr i64 %_19 to ptr
  store volatile i8 %b, ptr %_18, align 1
  %_90.1 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 2
  %_79.0.1 = or disjoint i64 %iter.sroa.5.019, 2
  %b.1 = load i8, ptr %_90, align 1, !noundef !3
  %_19.1 = add i64 %_79.0, %_51
  %_18.1 = inttoptr i64 %_19.1 to ptr
  store volatile i8 %b.1, ptr %_18.1, align 1
  %_90.2 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 3
  %_79.0.2 = or disjoint i64 %iter.sroa.5.019, 3
  %b.2 = load i8, ptr %_90.1, align 1, !noundef !3
  %_19.2 = add i64 %_79.0.1, %_51
  %_18.2 = inttoptr i64 %_19.2 to ptr
  store volatile i8 %b.2, ptr %_18.2, align 1
  %_90.3 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 4
  %_79.0.3 = add nuw nsw i64 %iter.sroa.5.019, 4
  %b.3 = load i8, ptr %_90.2, align 1, !noundef !3
  %_19.3 = add i64 %_79.0.2, %_51
  %_18.3 = inttoptr i64 %_19.3 to ptr
  store volatile i8 %b.3, ptr %_18.3, align 1
  %niter.next.3 = add i64 %niter, 4
  %niter.ncmp.3 = icmp eq i64 %niter.next.3, %unroll_iter
  br i1 %niter.ncmp.3, label %bb17.loopexit.unr-lcssa, label %bb18

bb17.loopexit.unr-lcssa:                          ; preds = %bb18, %bb18.preheader
  %iter.sroa.0.020.unr = phi ptr [ %src.0, %bb18.preheader ], [ %_90.3, %bb18 ]
  %iter.sroa.5.019.unr = phi i64 [ 0, %bb18.preheader ], [ %_79.0.3, %bb18 ]
  %lcmp.mod.not = icmp eq i64 %xtraiter, 0
  br i1 %lcmp.mod.not, label %bb17, label %bb18.epil

bb18.epil:                                        ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa
  %iter.sroa.0.020.epil = phi ptr [ %_90.epil, %bb18.epil ], [ %iter.sroa.0.020.unr, %bb17.loopexit.unr-lcssa ]
  %iter.sroa.5.019.epil = phi i64 [ %_79.0.epil, %bb18.epil ], [ %iter.sroa.5.019.unr, %bb17.loopexit.unr-lcssa ]
  %epil.iter = phi i64 [ %epil.iter.next, %bb18.epil ], [ 0, %bb17.loopexit.unr-lcssa ]
  %_90.epil = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020.epil, i64 1
  %_79.0.epil = add nuw nsw i64 %iter.sroa.5.019.epil, 1
  %b.epil = load i8, ptr %iter.sroa.0.020.epil, align 1, !noundef !3
  %_19.epil = add i64 %iter.sroa.5.019.epil, %_51
  %_18.epil = inttoptr i64 %_19.epil to ptr
  store volatile i8 %b.epil, ptr %_18.epil, align 1
  %epil.iter.next = add i64 %epil.iter, 1
  %epil.iter.cmp.not = icmp eq i64 %epil.iter.next, %xtraiter
  br i1 %epil.iter.cmp.not, label %bb17, label %bb18.epil, !llvm.loop !431

bb17:                                             ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa, %bb12
  %t = add i64 %words4.i, %spec.store.select.i
  %_97 = icmp ult i64 %t, %spec.store.select.i
  %_93.not = icmp ugt i64 %t, %src.1
  %or.cond = or i1 %_97, %_93.not
  br i1 %or.cond, label %bb21, label %bb20, !prof !416

bb21:                                             ; preds = %bb17
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %spec.store.select.i, i64 noundef %t, i64 noundef %src.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #24
  unreachable

bb20:                                             ; preds = %bb17
  %_8.i = and i64 %_6.i, 9223372036854775800
  %_106.not21 = icmp eq i64 %_8.i, 0
  br i1 %_106.not21, label %bb26, label %bb27.lr.ph

bb27.lr.ph:                                       ; preds = %bb20
  %_32 = add i64 %spec.store.select.i, %_51
  %3 = add nsw i64 %_8.i, -8
  %4 = lshr exact i64 %3, 3
  %5 = add nuw nsw i64 %4, 1
  %xtraiter28 = and i64 %5, 3
  %6 = icmp ult i64 %3, 24
  br i1 %6, label %bb26.loopexit.unr-lcssa, label %bb27.lr.ph.new

bb27.lr.ph.new:                                   ; preds = %bb27.lr.ph
  %unroll_iter31 = and i64 %5, 4611686018427387900
  br label %bb27

bb26.loopexit.unr-lcssa:                          ; preds = %bb27, %bb27.lr.ph
  %iter3.sroa.0.024.unr = phi i64 [ 0, %bb27.lr.ph ], [ %_103.0.3, %bb27 ]
  %iter2.sroa.0.022.unr = phi ptr [ %_73, %bb27.lr.ph ], [ %_111.3, %bb27 ]
  %lcmp.mod30.not = icmp eq i64 %xtraiter28, 0
  br i1 %lcmp.mod30.not, label %bb26, label %bb27.epil

bb27.epil:                                        ; preds = %bb27.epil, %bb26.loopexit.unr-lcssa
  %iter3.sroa.0.024.epil = phi i64 [ %_103.0.epil, %bb27.epil ], [ %iter3.sroa.0.024.unr, %bb26.loopexit.unr-lcssa ]
  %iter2.sroa.0.022.epil = phi ptr [ %_111.epil, %bb27.epil ], [ %iter2.sroa.0.022.unr, %bb26.loopexit.unr-lcssa ]
  %epil.iter29 = phi i64 [ %epil.iter29.next, %bb27.epil ], [ 0, %bb26.loopexit.unr-lcssa ]
  %_103.0.epil = add nuw nsw i64 %iter3.sroa.0.024.epil, 1
  %_111.epil = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.022.epil, i64 8
  %_120.sroa.0.0.copyload.epil = load i64, ptr %iter2.sroa.0.022.epil, align 1
  %_33.epil = shl i64 %iter3.sroa.0.024.epil, 3
  %_31.epil = add i64 %_32, %_33.epil
  %_30.epil = inttoptr i64 %_31.epil to ptr
  store volatile i64 %_120.sroa.0.0.copyload.epil, ptr %_30.epil, align 8
  %epil.iter29.next = add i64 %epil.iter29, 1
  %epil.iter29.cmp.not = icmp eq i64 %epil.iter29.next, %xtraiter28
  br i1 %epil.iter29.cmp.not, label %bb26, label %bb27.epil, !llvm.loop !432

bb26:                                             ; preds = %bb27.epil, %bb26.loopexit.unr-lcssa, %bb20
  %_36 = add nuw i64 %t, %_9.i
  %_121.not = icmp ugt i64 %_36, %src.1
  br i1 %_121.not, label %bb30, label %bb29, !prof !416

bb30:                                             ; preds = %bb26
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %t, i64 noundef %_36, i64 noundef %src.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #24
  unreachable

bb29:                                             ; preds = %bb26
  %_128 = getelementptr inbounds nuw i8, ptr %src.0, i64 %t
  %_14725 = icmp samesign eq i64 %_9.i, 0
  br i1 %_14725, label %bb6, label %bb36.lr.ph

bb36.lr.ph:                                       ; preds = %bb29
  %_42 = add i64 %t, %_51
  %b6 = load i8, ptr %_128, align 1, !noundef !3
  %_40 = inttoptr i64 %_42 to ptr
  store volatile i8 %b6, ptr %_40, align 1
  %_147 = icmp samesign eq i64 %_9.i, 1
  br i1 %_147, label %bb6, label %bb36.1

bb36.1:                                           ; preds = %bb36.lr.ph
  %_153 = getelementptr inbounds nuw i8, ptr %_128, i64 1
  %b6.1 = load i8, ptr %_153, align 1, !noundef !3
  %_41.1 = add i64 %_42, 1
  %_40.1 = inttoptr i64 %_41.1 to ptr
  store volatile i8 %b6.1, ptr %_40.1, align 1
  %_147.1 = icmp samesign eq i64 %_9.i, 2
  br i1 %_147.1, label %bb6, label %bb36.2

bb36.2:                                           ; preds = %bb36.1
  %_153.1 = getelementptr inbounds nuw i8, ptr %_128, i64 2
  %b6.2 = load i8, ptr %_153.1, align 1, !noundef !3
  %_41.2 = add i64 %_42, 2
  %_40.2 = inttoptr i64 %_41.2 to ptr
  store volatile i8 %b6.2, ptr %_40.2, align 1
  %_147.2 = icmp samesign eq i64 %_9.i, 3
  br i1 %_147.2, label %bb6, label %bb36.3

bb36.3:                                           ; preds = %bb36.2
  %_153.2 = getelementptr inbounds nuw i8, ptr %_128, i64 3
  %b6.3 = load i8, ptr %_153.2, align 1, !noundef !3
  %_41.3 = add i64 %_42, 3
  %_40.3 = inttoptr i64 %_41.3 to ptr
  store volatile i8 %b6.3, ptr %_40.3, align 1
  %_147.3 = icmp samesign eq i64 %_9.i, 4
  br i1 %_147.3, label %bb6, label %bb36.4

bb36.4:                                           ; preds = %bb36.3
  %_153.3 = getelementptr inbounds nuw i8, ptr %_128, i64 4
  %b6.4 = load i8, ptr %_153.3, align 1, !noundef !3
  %_41.4 = add i64 %_42, 4
  %_40.4 = inttoptr i64 %_41.4 to ptr
  store volatile i8 %b6.4, ptr %_40.4, align 1
  %_147.4 = icmp samesign eq i64 %_9.i, 5
  br i1 %_147.4, label %bb6, label %bb36.5

bb36.5:                                           ; preds = %bb36.4
  %_153.4 = getelementptr inbounds nuw i8, ptr %_128, i64 5
  %b6.5 = load i8, ptr %_153.4, align 1, !noundef !3
  %_41.5 = add i64 %_42, 5
  %_40.5 = inttoptr i64 %_41.5 to ptr
  store volatile i8 %b6.5, ptr %_40.5, align 1
  %_147.5 = icmp samesign eq i64 %_9.i, 6
  br i1 %_147.5, label %bb6, label %bb36.6

bb36.6:                                           ; preds = %bb36.5
  %_153.5 = getelementptr inbounds nuw i8, ptr %_128, i64 6
  %b6.6 = load i8, ptr %_153.5, align 1, !noundef !3
  %_41.6 = add i64 %_42, 6
  %_40.6 = inttoptr i64 %_41.6 to ptr
  store volatile i8 %b6.6, ptr %_40.6, align 1
  br label %bb6

bb6:                                              ; preds = %bb36.6, %bb36.5, %bb36.4, %bb36.3, %bb36.2, %bb36.1, %bb36.lr.ph, %bb29, %start
  %_0.sroa.0.0 = phi i8 [ 2, %start ], [ 7, %bb29 ], [ 7, %bb36.6 ], [ 7, %bb36.5 ], [ 7, %bb36.4 ], [ 7, %bb36.3 ], [ 7, %bb36.2 ], [ 7, %bb36.1 ], [ 7, %bb36.lr.ph ]
  ret i8 %_0.sroa.0.0

bb27:                                             ; preds = %bb27, %bb27.lr.ph.new
  %iter3.sroa.0.024 = phi i64 [ 0, %bb27.lr.ph.new ], [ %_103.0.3, %bb27 ]
  %iter2.sroa.0.022 = phi ptr [ %_73, %bb27.lr.ph.new ], [ %_111.3, %bb27 ]
  %niter32 = phi i64 [ 0, %bb27.lr.ph.new ], [ %niter32.next.3, %bb27 ]
  %_111 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.022, i64 8
  %_120.sroa.0.0.copyload = load i64, ptr %iter2.sroa.0.022, align 1
  %_33 = shl i64 %iter3.sroa.0.024, 3
  %_31 = add i64 %_32, %_33
  %_30 = inttoptr i64 %_31 to ptr
  store volatile i64 %_120.sroa.0.0.copyload, ptr %_30, align 8
  %_111.1 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.022, i64 16
  %_120.sroa.0.0.copyload.1 = load i64, ptr %_111, align 1
  %_103.0 = shl i64 %iter3.sroa.0.024, 3
  %_33.1 = or disjoint i64 %_103.0, 8
  %_31.1 = add i64 %_32, %_33.1
  %_30.1 = inttoptr i64 %_31.1 to ptr
  store volatile i64 %_120.sroa.0.0.copyload.1, ptr %_30.1, align 8
  %_111.2 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.022, i64 24
  %_120.sroa.0.0.copyload.2 = load i64, ptr %_111.1, align 1
  %_103.0.1 = shl i64 %iter3.sroa.0.024, 3
  %_33.2 = or disjoint i64 %_103.0.1, 16
  %_31.2 = add i64 %_32, %_33.2
  %_30.2 = inttoptr i64 %_31.2 to ptr
  store volatile i64 %_120.sroa.0.0.copyload.2, ptr %_30.2, align 8
  %_103.0.3 = add nuw nsw i64 %iter3.sroa.0.024, 4
  %_111.3 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.022, i64 32
  %_120.sroa.0.0.copyload.3 = load i64, ptr %_111.2, align 1
  %_103.0.2 = shl i64 %iter3.sroa.0.024, 3
  %_33.3 = or disjoint i64 %_103.0.2, 24
  %_31.3 = add i64 %_32, %_33.3
  %_30.3 = inttoptr i64 %_31.3 to ptr
  store volatile i64 %_120.sroa.0.0.copyload.3, ptr %_30.3, align 8
  %niter32.next.3 = add i64 %niter32, 4
  %niter32.ncmp.3 = icmp eq i64 %niter32.next.3, %unroll_iter31
  br i1 %niter32.ncmp.3, label %bb26.loopexit.unr-lcssa, label %bb27
}

; Function Attrs: nounwind nonlazybind uwtable
define internal range(i64 512, -4294965248) i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef readnone align 8 captures(none) dereferenceable(88) %self, ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %region, i64 noundef %off, i32 noundef %0) unnamed_addr #1 {
start:
  %_5.i = and i64 %off, 3
  %1 = icmp eq i64 %_5.i, 0
  br i1 %1, label %bb1.i, label %bb4

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 4
  %_16.1.i = icmp ugt i64 %off, -5
  %2 = getelementptr inbounds nuw i8, ptr %region, i64 8
  %_12.i = load i64, ptr %2, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb4, label %bb6, !prof !413

bb6:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %region, align 8, !alias.scope !433, !noalias !436, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %spec.store.select = tail call i32 @llvm.umin.i32(i32 %0, i32 2147483647)
  %n = tail call noundef i32 @__vm_notify(i64 noundef %_13.i, i32 noundef %spec.store.select) #17
  %n.lobit = lshr i32 %n, 31
  %. = zext nneg i32 %n.lobit to i64
  %3 = zext i32 %n to i64
  %4 = shl nuw i64 %3, 32
  br label %bb4

bb4:                                              ; preds = %bb6, %bb1.i, %start
  %_0.sroa.62.1 = phi i64 [ %4, %bb6 ], [ 0, %start ], [ 0, %bb1.i ]
  %_0.sroa.4.1 = phi i64 [ 1536, %bb6 ], [ 512, %start ], [ 512, %bb1.i ]
  %_0.sroa.0.1 = phi i64 [ %., %bb6 ], [ 1, %start ], [ 1, %bb1.i ]
  %_0.sroa.4.0.insert.insert = or disjoint i64 %_0.sroa.4.1, %_0.sroa.62.1
  %_0.sroa.0.0.insert.insert = or i64 %_0.sroa.4.0.insert.insert, %_0.sroa.0.1
  ret i64 %_0.sroa.0.0.insert.insert
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i32 @__vm_notify(i64 noundef, i32 noundef) unnamed_addr #1

; Function Attrs: mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable
define internal range(i64 0, -4294967294) i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off) unnamed_addr #19 {
start:
  %_5.i = and i64 %off, 3
  %0 = icmp eq i64 %_5.i, 0
  br i1 %0, label %bb1.i, label %bb3

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 4
  %_16.1.i = icmp ugt i64 %off, -5
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_12.i = load i64, ptr %1, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb3, label %bb5, !prof !413

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !438, !noalias !441, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  %2 = load atomic i32, ptr %_8 seq_cst, align 4
  %3 = zext i32 %2 to i64
  %4 = shl nuw i64 %3, 32
  br label %bb3

bb3:                                              ; preds = %bb5, %bb1.i, %start
  %_0.sroa.0.0 = phi i64 [ 0, %bb5 ], [ 1, %start ], [ 1, %bb1.i ]
  %_0.sroa.3.0.insert.insert = phi i64 [ %4, %bb5 ], [ 512, %start ], [ 512, %bb1.i ]
  %_0.sroa.0.0.insert.insert = or disjoint i64 %_0.sroa.3.0.insert.insert, %_0.sroa.0.0
  ret i64 %_0.sroa.0.0.insert.insert
}

; Function Attrs: mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable
define internal void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr dead_on_unwind noalias noundef writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) initializes((0, 1)) %_0, ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off) unnamed_addr #19 {
start:
  %_5.i = and i64 %off, 7
  %0 = icmp eq i64 %_5.i, 0
  br i1 %0, label %bb1.i, label %bb4

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 8
  %_16.1.i = icmp ugt i64 %off, -9
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_12.i = load i64, ptr %1, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb4, label %bb5, !prof !413

bb4:                                              ; preds = %bb1.i, %start
  %2 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 2, ptr %2, align 1
  br label %bb3

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !443, !noalias !446, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  %3 = load atomic i64, ptr %_8 seq_cst, align 8
  %4 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %3, ptr %4, align 8
  br label %bb3

bb3:                                              ; preds = %bb5, %bb4
  %storemerge = phi i8 [ 0, %bb5 ], [ 1, %bb4 ]
  store i8 %storemerge, ptr %_0, align 8
  ret void
}

; Function Attrs: mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, i32 noundef %v) unnamed_addr #19 {
start:
  %_5.i = and i64 %off, 3
  %0 = icmp eq i64 %_5.i, 0
  br i1 %0, label %bb1.i, label %bb3

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 4
  %_16.1.i = icmp ugt i64 %off, -5
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_12.i = load i64, ptr %1, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb3, label %bb5, !prof !413

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !448, !noalias !451, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  store atomic i32 %v, ptr %_8 seq_cst, align 4
  br label %bb3

bb3:                                              ; preds = %bb5, %bb1.i, %start
  %_0.sroa.0.0 = phi i8 [ 7, %bb5 ], [ 2, %start ], [ 2, %bb1.i ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, i64 noundef %v) unnamed_addr #19 {
start:
  %_5.i = and i64 %off, 7
  %0 = icmp eq i64 %_5.i, 0
  br i1 %0, label %bb1.i, label %bb3

bb1.i:                                            ; preds = %start
  %_16.0.i = add nuw i64 %off, 8
  %_16.1.i = icmp ugt i64 %off, -9
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_12.i = load i64, ptr %1, align 8
  %_11.i = icmp ugt i64 %_16.0.i, %_12.i
  %or.cond = select i1 %_16.1.i, i1 true, i1 %_11.i
  br i1 %or.cond, label %bb3, label %bb5, !prof !413

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !453, !noalias !456, !noundef !3
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  store atomic i64 %v, ptr %_8 seq_cst, align 8
  br label %bb3

bb3:                                              ; preds = %bb5, %bb1.i, %start
  %_0.sroa.0.0 = phi i8 [ 7, %bb5 ], [ 2, %start ], [ 2, %bb1.i ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: readwrite) uwtable
define internal void @_RNvMs0_CsdT7xHiCjqae_9unir_wireNtB5_11FrameHeader6encode(ptr dead_on_unwind noalias noundef writable writeonly sret([9 x i8]) align 1 captures(none) dereferenceable(9) initializes((0, 2)) %_0, ptr noalias noundef readonly align 4 captures(none) dereferenceable(8) %self) unnamed_addr #20 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 6
  %_29 = load i16, ptr %0, align 2, !noundef !3
  %1 = icmp ult i16 %_29, 4
  br i1 %1, label %bb3, label %bb13

bb3:                                              ; preds = %start
  %_31 = load i32, ptr %self, align 4, !noundef !3
  %_30 = icmp ugt i32 %_31, -8
  br i1 %_30, label %bb13, label %bb5

bb5:                                              ; preds = %bb3
  %_32 = and i16 %_29, 1
  %2 = icmp eq i16 %_32, 0
  %3 = icmp eq i32 %_31, 16
  %or.cond = or i1 %2, %3
  br i1 %or.cond, label %bb9, label %bb13

bb9:                                              ; preds = %bb5
  %b.sroa.0.0.insert.ext = zext i32 %_31 to i64
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 4
  %_19 = load i16, ptr %4, align 4, !noundef !3
  %b.sroa.0.4.insert.ext = zext i16 %_19 to i64
  %b.sroa.0.4.insert.shift = shl nuw nsw i64 %b.sroa.0.4.insert.ext, 32
  %b.sroa.0.6.insert.ext = zext nneg i16 %_29 to i64
  %b.sroa.0.6.insert.shift = shl nuw nsw i64 %b.sroa.0.6.insert.ext, 48
  %b.sroa.0.4.insert.insert = or disjoint i64 %b.sroa.0.6.insert.shift, %b.sroa.0.0.insert.ext
  %b.sroa.0.6.insert.insert = or disjoint i64 %b.sroa.0.4.insert.insert, %b.sroa.0.4.insert.shift
  %5 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i64 %b.sroa.0.6.insert.insert, ptr %5, align 1
  br label %bb1

bb1:                                              ; preds = %bb13, %bb9
  %storemerge = phi i8 [ 0, %bb9 ], [ 1, %bb13 ]
  store i8 %storemerge, ptr %_0, align 1
  ret void

bb13:                                             ; preds = %bb5, %bb3, %start
  %_3.sroa.0.0 = phi i8 [ 1, %start ], [ 8, %bb3 ], [ 2, %bb5 ]
  %6 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 %_3.sroa.0.0, ptr %6, align 1
  br label %bb1
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable
define internal { i1, i8 } @_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal15decode_consumer(i32 noundef %w) unnamed_addr #18 {
start:
  %0 = icmp ult i32 %w, 65536
  br i1 %0, label %bb2.i, label %bb7.i

bb2.i:                                            ; preds = %start
  %_5.i = lshr i32 %w, 8
  %trunc.i = trunc i32 %w to i8
  switch i8 %trunc.i, label %bb7.i [
    i8 0, label %bb6.i
    i8 1, label %bb5.i
    i8 2, label %bb4.i
  ]

bb6.i:                                            ; preds = %bb2.i
  %1 = icmp samesign ult i32 %w, 256
  br i1 %1, label %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit, label %bb7.i

bb5.i:                                            ; preds = %bb2.i
  %2 = icmp samesign ult i32 %w, 256
  br i1 %2, label %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit, label %bb7.i

bb4.i:                                            ; preds = %bb2.i
  %_13.i = icmp samesign ult i32 %w, 2560
  br i1 %_13.i, label %bb14.i, label %bb7.i

bb14.i:                                           ; preds = %bb4.i
  %_12.i = zext nneg i32 %_5.i to i64
  %3 = getelementptr inbounds nuw i8, ptr @alloc_cc193f9e0c79ce36721b88a4026725d4.109, i64 %_12.i
  %_14.i = load i8, ptr %3, align 1, !range !222, !noundef !3
  br label %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit

_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit: ; preds = %bb14.i, %bb5.i, %bb6.i
  %_0.sroa.8.0.i = phi i8 [ %_14.i, %bb14.i ], [ 10, %bb6.i ], [ 11, %bb5.i ]
  %4 = icmp eq i8 %_0.sroa.8.0.i, 11
  %. = select i1 %4, i8 4, i8 %_0.sroa.8.0.i
  br label %bb7.i

bb7.i:                                            ; preds = %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit, %bb4.i, %bb5.i, %bb6.i, %bb2.i, %start
  %5 = phi i8 [ %., %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit ], [ 2, %start ], [ 4, %bb2.i ], [ 3, %bb4.i ], [ 2, %bb5.i ], [ 2, %bb6.i ]
  %_0.sroa.0.1 = phi i1 [ %4, %_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal6decode.exit ], [ true, %start ], [ true, %bb2.i ], [ true, %bb4.i ], [ true, %bb5.i ], [ true, %bb6.i ]
  %6 = insertvalue { i1, i8 } poison, i1 %_0.sroa.0.1, 0
  %7 = insertvalue { i1, i8 } %6, i8 %5, 1
  ret { i1, i8 } %7
}

attributes #0 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #1 = { nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #2 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write) }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #4 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite) }
attributes #5 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #6 = { cold minsize noreturn nounwind nonlazybind optsize uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #7 = { noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #8 = { nocallback nofree nosync nounwind willreturn memory(argmem: read) }
attributes #9 = { cold noinline noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #10 = { nofree norecurse nosync nounwind nonlazybind memory(argmem: read, inaccessiblemem: write) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #11 = { cold minsize noinline noreturn nounwind nonlazybind optsize uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #12 = { noinline nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #13 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #14 = { cold nofree noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #15 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: read) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #16 = { inlinehint nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #17 = { nounwind }
attributes #18 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #19 = { mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #20 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: readwrite) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #21 = { noreturn nounwind }
attributes #22 = { inlinehint nounwind }
attributes #23 = { noinline nounwind }
attributes #24 = { noinline noreturn nounwind }

!llvm.ident = !{!0, !0, !0, !0, !0, !0, !0, !0, !0}
!llvm.module.flags = !{!1, !2}

!0 = !{!"rustc version 1.94.1 (e408947bf 2026-03-25)"}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 2, !"RtLibUseGOT", i32 1}
!3 = !{}
!4 = !{i64 1}
!5 = !{i64 8}
!6 = !{!7, !9, !11}
!7 = distinct !{!7, !8, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!8 = distinct !{!8, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!9 = distinct !{!9, !10, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!10 = distinct !{!10, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!11 = distinct !{!11, !12, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!12 = distinct !{!12, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!13 = !{!14}
!14 = distinct !{!14, !15, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!15 = distinct !{!15, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!16 = distinct !{!16, !17, !18}
!17 = !{!"llvm.loop.isvectorized", i32 1}
!18 = !{!"llvm.loop.unroll.runtime.disable"}
!19 = distinct !{!19, !18, !17}
!20 = !{!21, !23, !25}
!21 = distinct !{!21, !22, !"_RINvNtNtCscliFh4jUES5_4core3str11validations15next_code_pointINtNtNtB6_5slice4iter4IterhEEB6_: %bytes"}
!22 = distinct !{!22, !"_RINvNtNtCscliFh4jUES5_4core3str11validations15next_code_pointINtNtNtB6_5slice4iter4IterhEEB6_"}
!23 = distinct !{!23, !24, !"_RNvXs3_NtNtCscliFh4jUES5_4core3str4iterNtB5_11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator4next: %self"}
!24 = distinct !{!24, !"_RNvXs3_NtNtCscliFh4jUES5_4core3str4iterNtB5_11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator4next"}
!25 = distinct !{!25, !26, !"_RINvYNtNtNtCscliFh4jUES5_4core3str4iter11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator8try_foldINtNtNtB9_3num7nonzero7NonZerojENCNvXs_NvBO_10advance_byB3_NtB2f_13SpecAdvanceBy15spec_advance_by0INtNtB9_6option6OptionB1C_EEB9_: %self"}
!26 = distinct !{!26, !"_RINvYNtNtNtCscliFh4jUES5_4core3str4iter11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator8try_foldINtNtNtB9_3num7nonzero7NonZerojENCNvXs_NvBO_10advance_byB3_NtB2f_13SpecAdvanceBy15spec_advance_by0INtNtB9_6option6OptionB1C_EEB9_"}
!27 = !{!28}
!28 = distinct !{!28, !29, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!29 = distinct !{!29, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!30 = !{!31}
!31 = distinct !{!31, !32, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write: %f"}
!32 = distinct !{!32, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write"}
!33 = !{!"branch_weights", i32 0, i32 -2147483648}
!34 = !{!35, !37, !39}
!35 = distinct !{!35, !36, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!36 = distinct !{!36, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!37 = distinct !{!37, !38, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!38 = distinct !{!38, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!39 = distinct !{!39, !40, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!40 = distinct !{!40, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!41 = !{!42}
!42 = distinct !{!42, !43, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!43 = distinct !{!43, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!44 = distinct !{!44, !17, !18}
!45 = distinct !{!45, !18, !17}
!46 = !{!47, !49, !51}
!47 = distinct !{!47, !48, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!48 = distinct !{!48, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!49 = distinct !{!49, !50, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!50 = distinct !{!50, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!51 = distinct !{!51, !52, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!52 = distinct !{!52, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!53 = !{!54}
!54 = distinct !{!54, !55, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!55 = distinct !{!55, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!56 = !{!57, !59, !61}
!57 = distinct !{!57, !58, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!58 = distinct !{!58, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!59 = distinct !{!59, !60, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!60 = distinct !{!60, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!61 = distinct !{!61, !62, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!62 = distinct !{!62, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!63 = !{!64}
!64 = distinct !{!64, !65, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!65 = distinct !{!65, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!66 = distinct !{!66, !17, !18}
!67 = distinct !{!67, !18, !17}
!68 = distinct !{!68, !17, !18}
!69 = distinct !{!69, !18, !17}
!70 = !{!71, !73, !75}
!71 = distinct !{!71, !72, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!72 = distinct !{!72, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!73 = distinct !{!73, !74, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!74 = distinct !{!74, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!75 = distinct !{!75, !76, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!76 = distinct !{!76, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!77 = !{!78}
!78 = distinct !{!78, !79, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!79 = distinct !{!79, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!80 = distinct !{!80, !17, !18}
!81 = distinct !{!81, !18, !17}
!82 = !{!83}
!83 = distinct !{!83, !84, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!84 = distinct !{!84, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!85 = !{!86}
!86 = distinct !{!86, !87, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write: %f"}
!87 = distinct !{!87, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write"}
!88 = !{!89}
!89 = distinct !{!89, !90, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!90 = distinct !{!90, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!91 = !{!92}
!92 = distinct !{!92, !93, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!93 = distinct !{!93, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!94 = !{i8 0, i8 5}
!95 = !{!96, !97}
!96 = distinct !{!96, !93, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!97 = distinct !{!97, !93, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!98 = !{!96}
!99 = !{!96, !92, !97}
!100 = !{i8 0, i8 4}
!101 = !{!102}
!102 = distinct !{!102, !103, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!103 = distinct !{!103, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!104 = !{!105}
!105 = distinct !{!105, !106, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!106 = distinct !{!106, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!107 = !{!108}
!108 = distinct !{!108, !109, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!109 = distinct !{!109, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!110 = !{!111}
!111 = distinct !{!111, !112, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!112 = distinct !{!112, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!113 = !{!114}
!114 = distinct !{!114, !115, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!115 = distinct !{!115, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!116 = !{i64 0, i64 -9223372036854775808}
!117 = !{!114, !111, !108, !105, !102}
!118 = !{i8 0, i8 2}
!119 = !{!120}
!120 = distinct !{!120, !121, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!121 = distinct !{!121, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!122 = !{!123}
!123 = distinct !{!123, !121, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!124 = !{!125, !127}
!125 = distinct !{!125, !126, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_: %self"}
!126 = distinct !{!126, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_"}
!127 = distinct !{!127, !128, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerEEB1h_: %_1"}
!128 = distinct !{!128, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerEEB1h_"}
!129 = !{!130, !132}
!130 = distinct !{!130, !131, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_: %_0"}
!131 = distinct !{!131, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_"}
!132 = distinct !{!132, !131, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_: %_1"}
!133 = !{!134, !136, !130, !132}
!134 = distinct !{!134, !135, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!135 = distinct !{!135, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!136 = distinct !{!136, !135, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!137 = !{!134, !130, !132}
!138 = !{i8 0, i8 6}
!139 = !{!136, !130, !132}
!140 = !{!"branch_weights", !"expected", i32 1, i32 2000}
!141 = !{i64 0, i64 2}
!142 = !{!143}
!143 = distinct !{!143, !144, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!144 = distinct !{!144, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!145 = !{!146}
!146 = distinct !{!146, !144, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!147 = !{!148}
!148 = distinct !{!148, !149, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %_0"}
!149 = distinct !{!149, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi"}
!150 = !{!151}
!151 = distinct !{!151, !149, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %x"}
!152 = !{!148, !151, !153, !154}
!153 = distinct !{!153, !149, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %s"}
!154 = distinct !{!154, !149, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: argument 3"}
!155 = !{!156, !151}
!156 = distinct !{!156, !157, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!157 = distinct !{!157, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!158 = !{!148, !153, !154}
!159 = !{i64 0, i64 3}
!160 = !{!151, !153, !154}
!161 = !{!162}
!162 = distinct !{!162, !163, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!163 = distinct !{!163, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!164 = !{!165}
!165 = distinct !{!165, !163, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!166 = !{!165, !151}
!167 = !{!162, !168, !148, !153, !154}
!168 = distinct !{!168, !163, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!169 = !{!162, !148}
!170 = !{!171, !165, !151}
!171 = distinct !{!171, !172, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!172 = distinct !{!172, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!173 = !{!165, !168, !148, !151, !153, !154}
!174 = !{!175}
!175 = distinct !{!175, !176, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!176 = distinct !{!176, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!177 = !{!178}
!178 = distinct !{!178, !179, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!179 = distinct !{!179, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!180 = !{!181}
!181 = distinct !{!181, !182, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!182 = distinct !{!182, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!183 = !{!184}
!184 = distinct !{!184, !185, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!185 = distinct !{!185, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!186 = !{!187}
!187 = distinct !{!187, !188, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!188 = distinct !{!188, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!189 = !{!187, !184, !181, !178, !175}
!190 = !{!191}
!191 = distinct !{!191, !192, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!192 = distinct !{!192, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!193 = !{!194}
!194 = distinct !{!194, !192, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!195 = !{!191, !194, !196, !197}
!196 = distinct !{!196, !192, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!197 = distinct !{!197, !192, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %buf.0"}
!198 = !{!199}
!199 = distinct !{!199, !200, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!200 = distinct !{!200, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!201 = !{!202}
!202 = distinct !{!202, !200, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!203 = !{!202, !194}
!204 = !{!199, !205, !191, !196, !197}
!205 = distinct !{!205, !200, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!206 = !{!202, !205, !191, !194, !196, !197}
!207 = !{!199, !202, !205, !191, !194, !196, !197}
!208 = !{!209}
!209 = distinct !{!209, !210, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!210 = distinct !{!210, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!211 = !{!212, !214, !215, !216, !199, !205, !191, !197}
!212 = distinct !{!212, !213, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_0"}
!213 = distinct !{!213, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi"}
!214 = distinct !{!214, !213, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!215 = distinct !{!215, !210, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!216 = distinct !{!216, !210, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!217 = !{!212, !214, !215, !209, !216, !199, !202, !205, !191, !194, !196, !197}
!218 = !{i8 0, i8 7}
!219 = !{!209, !202, !194}
!220 = !{!215, !216, !199, !205, !191, !196, !197}
!221 = !{!199, !191, !197}
!222 = !{i8 0, i8 10}
!223 = !{!215, !209, !216, !199, !202, !205, !191, !194, !196, !197}
!224 = !{!191, !197}
!225 = !{!226, !202, !194}
!226 = distinct !{!226, !227, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!227 = distinct !{!227, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!228 = !{!229, !230, !199, !205, !191, !196, !197}
!229 = distinct !{!229, !227, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!230 = distinct !{!230, !227, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!231 = !{!229, !230, !199, !191, !197}
!232 = !{!194, !196, !197}
!233 = !{!191, !196, !197}
!234 = !{!235}
!235 = distinct !{!235, !236, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!236 = distinct !{!236, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!237 = !{!235, !194}
!238 = !{!239, !240, !191, !196, !197}
!239 = distinct !{!239, !236, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!240 = distinct !{!240, !236, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!241 = !{!239, !240, !191}
!242 = !{!239, !235, !240, !191, !194, !196, !197}
!243 = !{!244, !246, !239, !240, !191}
!244 = distinct !{!244, !245, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!245 = distinct !{!245, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!246 = distinct !{!246, !245, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!247 = !{!248, !235, !194}
!248 = distinct !{!248, !245, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!249 = !{!244, !246, !239, !240, !191, !196, !197}
!250 = !{!239, !191}
!251 = !{!235, !240, !191, !194, !196, !197}
!252 = !{!253}
!253 = distinct !{!253, !254, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_: %_1"}
!254 = distinct !{!254, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_"}
!255 = !{!256}
!256 = distinct !{!256, !257, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!257 = distinct !{!257, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!258 = !{!256, !253}
!259 = !{!260, !261}
!260 = distinct !{!260, !257, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!261 = distinct !{!261, !257, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!262 = !{!260}
!263 = !{!260, !256, !261, !253}
!264 = !{!265}
!265 = distinct !{!265, !266, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!266 = distinct !{!266, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!267 = !{!268}
!268 = distinct !{!268, !269, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!269 = distinct !{!269, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!270 = !{!271}
!271 = distinct !{!271, !272, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!272 = distinct !{!272, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!273 = !{!274}
!274 = distinct !{!274, !275, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!275 = distinct !{!275, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!276 = !{!277}
!277 = distinct !{!277, !278, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!278 = distinct !{!278, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!279 = !{!277, !274, !271, !268, !265}
!280 = !{!281}
!281 = distinct !{!281, !282, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!282 = distinct !{!282, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!283 = !{!284, !285}
!284 = distinct !{!284, !282, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!285 = distinct !{!285, !282, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!286 = !{!284}
!287 = !{!284, !281, !285}
!288 = !{!289}
!289 = distinct !{!289, !290, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!290 = distinct !{!290, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!291 = !{!292}
!292 = distinct !{!292, !293, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!293 = distinct !{!293, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!294 = !{!295}
!295 = distinct !{!295, !296, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!296 = distinct !{!296, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!297 = !{!298}
!298 = distinct !{!298, !299, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!299 = distinct !{!299, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!300 = !{!301}
!301 = distinct !{!301, !302, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!302 = distinct !{!302, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!303 = !{!301, !298, !295, !292, !289}
!304 = !{!305, !307}
!305 = distinct !{!305, !306, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_: %self"}
!306 = distinct !{!306, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_"}
!307 = distinct !{!307, !308, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerEEB1h_: %_1"}
!308 = distinct !{!308, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerEEB1h_"}
!309 = !{!310, !312}
!310 = distinct !{!310, !311, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_: %_0"}
!311 = distinct !{!311, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_"}
!312 = distinct !{!312, !311, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_: %_1"}
!313 = !{!314}
!314 = distinct !{!314, !315, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_: %_1"}
!315 = distinct !{!315, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_"}
!316 = !{!317}
!317 = distinct !{!317, !318, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!318 = distinct !{!318, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!319 = !{!317, !314}
!320 = !{!321, !322}
!321 = distinct !{!321, !318, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!322 = distinct !{!322, !318, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!323 = !{!321}
!324 = !{!321, !317, !322, !314}
!325 = !{!326}
!326 = distinct !{!326, !327, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!327 = distinct !{!327, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!328 = !{!329}
!329 = distinct !{!329, !330, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!330 = distinct !{!330, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!331 = !{!332}
!332 = distinct !{!332, !333, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!333 = distinct !{!333, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!334 = !{!335}
!335 = distinct !{!335, !336, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!336 = distinct !{!336, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!337 = !{!338}
!338 = distinct !{!338, !339, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!339 = distinct !{!339, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!340 = !{!338, !335, !332, !329, !326}
!341 = !{!342}
!342 = distinct !{!342, !343, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %x"}
!343 = distinct !{!343, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi"}
!344 = !{!345, !342}
!345 = distinct !{!345, !346, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!346 = distinct !{!346, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!347 = !{!348, !349, !350}
!348 = distinct !{!348, !343, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %_0"}
!349 = distinct !{!349, !343, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %s"}
!350 = distinct !{!350, !343, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %attempt"}
!351 = !{!348, !350}
!352 = !{!348, !342, !349, !350}
!353 = !{!348}
!354 = !{!350}
!355 = !{!356}
!356 = distinct !{!356, !357, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!357 = distinct !{!357, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!358 = !{!359}
!359 = distinct !{!359, !357, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!360 = !{!359, !342}
!361 = !{!356, !362, !348, !349, !350}
!362 = distinct !{!362, !357, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!363 = !{!356, !348, !350}
!364 = !{!365, !359, !342}
!365 = distinct !{!365, !366, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!366 = distinct !{!366, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!367 = !{!359, !362, !348, !342, !349, !350}
!368 = !{!342, !349, !350}
!369 = !{!370}
!370 = distinct !{!370, !371, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!371 = distinct !{!371, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!372 = !{!373}
!373 = distinct !{!373, !374, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!374 = distinct !{!374, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!375 = !{!376}
!376 = distinct !{!376, !377, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!377 = distinct !{!377, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!378 = !{!379}
!379 = distinct !{!379, !380, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!380 = distinct !{!380, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!381 = !{!382}
!382 = distinct !{!382, !383, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!383 = distinct !{!383, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!384 = !{!382, !379, !376, !373, !370}
!385 = !{i64 2}
!386 = !{!387}
!387 = distinct !{!387, !388, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!388 = distinct !{!388, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!389 = !{!390}
!390 = distinct !{!390, !388, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!391 = !{!387, !392, !393}
!392 = distinct !{!392, !388, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!393 = distinct !{!393, !388, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %payload.0"}
!394 = !{!390, !392, !393}
!395 = !{!387, !390, !392, !393}
!396 = !{!397}
!397 = distinct !{!397, !398, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!398 = distinct !{!398, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!399 = !{!400, !402, !403, !404, !387, !392, !393}
!400 = distinct !{!400, !401, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_0"}
!401 = distinct !{!401, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi"}
!402 = distinct !{!402, !401, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!403 = distinct !{!403, !398, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!404 = distinct !{!404, !398, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!405 = !{!400, !402, !403, !397, !404, !387, !390, !392, !393}
!406 = !{!397, !390}
!407 = !{!403, !404, !387, !392, !393}
!408 = !{!387, !393}
!409 = !{i8 0, i8 9}
!410 = !{!411, !387}
!411 = distinct !{!411, !412, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!412 = distinct !{!412, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi"}
!413 = !{!"branch_weights", i32 2002, i32 2000}
!414 = distinct !{!414, !415}
!415 = !{!"llvm.loop.unroll.disable"}
!416 = !{!"branch_weights", i32 4001, i32 4000000}
!417 = !{!418}
!418 = distinct !{!418, !419, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen: %dest.0"}
!419 = distinct !{!419, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen"}
!420 = !{!421}
!421 = distinct !{!421, !419, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen: %src.0"}
!422 = distinct !{!422, !415}
!423 = !{!424}
!424 = distinct !{!424, !425, !"_RNvCsIEB7taFyf8_10unir_temen11region_size: %_0"}
!425 = distinct !{!425, !"_RNvCsIEB7taFyf8_10unir_temen11region_size"}
!426 = !{!427}
!427 = distinct !{!427, !428, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!428 = distinct !{!428, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!429 = !{!430}
!430 = distinct !{!430, !428, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!431 = distinct !{!431, !415}
!432 = distinct !{!432, !415}
!433 = !{!434}
!434 = distinct !{!434, !435, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!435 = distinct !{!435, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!436 = !{!437}
!437 = distinct !{!437, !435, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!438 = !{!439}
!439 = distinct !{!439, !440, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!440 = distinct !{!440, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!441 = !{!442}
!442 = distinct !{!442, !440, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!443 = !{!444}
!444 = distinct !{!444, !445, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!445 = distinct !{!445, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!446 = !{!447}
!447 = distinct !{!447, !445, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!448 = !{!449}
!449 = distinct !{!449, !450, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!450 = distinct !{!450, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!451 = !{!452}
!452 = distinct !{!452, !450, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!453 = !{!454}
!454 = distinct !{!454, !455, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!455 = distinct !{!455, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!456 = !{!457}
!457 = distinct !{!457, !455, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
