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

%"core::option::Option<(i64, u32, i32)>" = type { i64, [2 x i64] }

@alloc_ed2c01013b845a2275ba463d39b0766f = private unnamed_addr constant [11 x i8] c"<redacted>\00", align 1
@alloc_11e2992a5b9c8f277d4a00890b54e14e = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_ed2c01013b845a2275ba463d39b0766f, [16 x i8] c"\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00" }>, align 8
@alloc_31365cfefba383c4d2bf6b6a04cc10aa = private unnamed_addr constant [17 x i8] c"capacity overflow", align 1
@alloc_ed2c01013b845a2275ba463d39b0766f.10 = private unnamed_addr constant [11 x i8] c"<redacted>\00", align 1
@alloc_11e2992a5b9c8f277d4a00890b54e14e.9 = private unnamed_addr constant <{ ptr, [16 x i8] }> <{ ptr @alloc_ed2c01013b845a2275ba463d39b0766f.10, [16 x i8] c"\0A\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00" }>, align 8
@alloc_850f178d3cd0d4307d8d45b841b2c2ad = private unnamed_addr constant [6 x i8] c"\C0\02: \C0\00", align 1
@alloc_a434a7e153ac922489f2ed192aeda5a5 = private unnamed_addr constant [55 x i8] c" index out of bounds: the len is \C0\12 but the index is \C0\00", align 1
@alloc_7ef6fe6e5749e409bd35c794f395407d = private unnamed_addr constant [200 x i8] c"00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899", align 1
@alloc_db51a71a1b6b25b4224d4dc5277f93e7 = private unnamed_addr constant [256 x i8] c"\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\02\03\03\03\03\03\03\03\03\03\03\03\03\03\03\03\03\04\04\04\04\04\00\00\00\00\00\00\00\00\00\00\00", align 1
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
@alloc_ddb4cde4dbe5ec370c8c8cf52e622a70 = private unnamed_addr constant [9 x i8] c"unir.args", align 1
@alloc_cba4392de44c79dd503043d101897357 = private unnamed_addr constant [10 x i8] c"unir.child", align 1
@alloc_cc193f9e0c79ce36721b88a4026725d4.109 = private unnamed_addr constant [10 x i8] c"\00\01\02\03\04\05\06\07\08\09", align 1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #0

; Function Attrs: nounwind nonlazybind uwtable
declare void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() unnamed_addr #1

; Function Attrs: cold minsize noreturn nounwind nonlazybind optsize uwtable
define internal void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec12handle_error(i64 noundef range(i64 0, -9223372036854775807) %0, i64 %1) unnamed_addr #2 {
start:
  %.not = icmp eq i64 %0, 0
  br i1 %.not, label %bb3, label %bb2, !prof !3

bb2:                                              ; preds = %start
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef %0, i64 noundef %1) #28
  unreachable

bb3:                                              ; preds = %start
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec17capacity_overflow() #29
  unreachable
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write)
declare void @llvm.assume(i1 noundef) #3

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #4

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite)
declare void @llvm.experimental.noalias.scope.decl(metadata) #5

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.umax.i64(i64, i64) #6

; Function Attrs: cold minsize noreturn nounwind nonlazybind optsize uwtable
define internal void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef range(i64 1, -9223372036854775807) %layout.0, i64 noundef %layout.1) unnamed_addr #2 {
start:
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc26___rust_alloc_error_handler(i64 noundef %layout.1, i64 noundef %layout.0) #28
  unreachable
}

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec17capacity_overflow() unnamed_addr #7 {
start:
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_31365cfefba383c4d2bf6b6a04cc10aa, ptr noundef nonnull inttoptr (i64 35 to ptr), ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e) #29
  unreachable
}

; Function Attrs: noreturn nounwind nonlazybind uwtable
declare void @_RNvCs1Y7DaGC1cwg_7___rustc26___rust_alloc_error_handler(i64 noundef, i64 noundef) unnamed_addr #8

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.usub.sat.i64(i64, i64) #6

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.umin.i64(i64, i64) #6

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umin.i32(i32, i32) #6

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: read)
declare ptr @llvm.load.relative.i64(ptr, i64) #9

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXs1i_NtCscliFh4jUES5_4core3fmtReNtB6_7Display3fmtB8_(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %f) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load i64, ptr %0, align 8, !noundef !4
  %_0.i = tail call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter3pad(ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %f, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_3.0, i64 noundef %_3.1) #22
  ret i1 %_0.i
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXs1g_NtCscliFh4jUES5_4core3fmtRDNtB6_5DebugEL_Bx_3fmtB8_(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, ptr noalias noundef align 8 dereferenceable(24) %f) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load ptr, ptr %0, align 8, !nonnull !4, !align !6, !noundef !4
  %1 = getelementptr inbounds nuw i8, ptr %_3.1, i64 24
  %2 = load ptr, ptr %1, align 8, !invariant.load !4, !nonnull !4
  %_0 = tail call noundef zeroext i1 %2(ptr noundef nonnull align 1 %_3.0, ptr noalias noundef nonnull align 8 dereferenceable(24) %f) #30
  ret i1 %_0
}

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull %0, ptr noundef nonnull %1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %2) unnamed_addr #7 {
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
  call void @_RNvCs1Y7DaGC1cwg_7___rustc17rust_begin_unwind(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %pi) #28
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter3pad(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %self, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) unnamed_addr #1 {
start:
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_4 = load i32, ptr %2, align 8, !noundef !4
  %_3 = and i32 %_4, 402653184
  %3 = icmp eq i32 %_3, 0
  br i1 %3, label %bb1, label %bb3

bb1:                                              ; preds = %start
  %_26.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_26.1 = load ptr, ptr %4, align 8, !nonnull !4, !align !6, !noundef !4
  %5 = getelementptr inbounds nuw i8, ptr %_26.1, i64 24
  %6 = load ptr, ptr %5, align 8, !invariant.load !4, !nonnull !4
  %7 = tail call noundef zeroext i1 %6(ptr noundef nonnull align 1 %_26.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) #30
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
  %9 = tail call noundef i64 @_RNvNtNtCscliFh4jUES5_4core3str5count14do_count_chars(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1) #22
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
  %wide.load = load <2 x i8>, ptr %0, align 1, !alias.scope !7, !noalias !14
  %wide.load29 = load <2 x i8>, ptr %11, align 1, !alias.scope !7, !noalias !14
  %12 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %13 = icmp sgt <2 x i8> %wide.load29, splat (i8 -65)
  %14 = zext <2 x i1> %12 to <2 x i64>
  %15 = zext <2 x i1> %13 to <2 x i64>
  %16 = icmp eq i64 %n.vec, 4
  br i1 %16, label %middle.block, label %vector.body.1, !llvm.loop !17

vector.body.1:                                    ; preds = %vector.ph
  %17 = getelementptr inbounds nuw i8, ptr %0, i64 4
  %18 = getelementptr inbounds nuw i8, ptr %0, i64 6
  %wide.load.1 = load <2 x i8>, ptr %17, align 1, !alias.scope !7, !noalias !14
  %wide.load29.1 = load <2 x i8>, ptr %18, align 1, !alias.scope !7, !noalias !14
  %19 = icmp sgt <2 x i8> %wide.load.1, splat (i8 -65)
  %20 = icmp sgt <2 x i8> %wide.load29.1, splat (i8 -65)
  %21 = zext <2 x i1> %19 to <2 x i64>
  %22 = zext <2 x i1> %20 to <2 x i64>
  %23 = add nuw nsw <2 x i64> %14, %21
  %24 = add nuw nsw <2 x i64> %15, %22
  %25 = icmp eq i64 %n.vec, 8
  br i1 %25, label %middle.block, label %vector.body.2, !llvm.loop !17

vector.body.2:                                    ; preds = %vector.body.1
  %26 = getelementptr inbounds nuw i8, ptr %0, i64 8
  %27 = getelementptr inbounds nuw i8, ptr %0, i64 10
  %wide.load.2 = load <2 x i8>, ptr %26, align 1, !alias.scope !7, !noalias !14
  %wide.load29.2 = load <2 x i8>, ptr %27, align 1, !alias.scope !7, !noalias !14
  %28 = icmp sgt <2 x i8> %wide.load.2, splat (i8 -65)
  %29 = icmp sgt <2 x i8> %wide.load29.2, splat (i8 -65)
  %30 = zext <2 x i1> %28 to <2 x i64>
  %31 = zext <2 x i1> %29 to <2 x i64>
  %32 = add nuw nsw <2 x i64> %23, %30
  %33 = add nuw nsw <2 x i64> %24, %31
  %34 = icmp eq i64 %n.vec, 12
  br i1 %34, label %middle.block, label %vector.body.3, !llvm.loop !17

vector.body.3:                                    ; preds = %vector.body.2
  %35 = getelementptr inbounds nuw i8, ptr %0, i64 12
  %36 = getelementptr inbounds nuw i8, ptr %0, i64 14
  %wide.load.3 = load <2 x i8>, ptr %35, align 1, !alias.scope !7, !noalias !14
  %wide.load29.3 = load <2 x i8>, ptr %36, align 1, !alias.scope !7, !noalias !14
  %37 = icmp sgt <2 x i8> %wide.load.3, splat (i8 -65)
  %38 = icmp sgt <2 x i8> %wide.load29.3, splat (i8 -65)
  %39 = zext <2 x i1> %37 to <2 x i64>
  %40 = zext <2 x i1> %38 to <2 x i64>
  %41 = add nuw nsw <2 x i64> %32, %39
  %42 = add nuw nsw <2 x i64> %33, %40
  %43 = icmp eq i64 %n.vec, 16
  br i1 %43, label %middle.block, label %vector.body.4, !llvm.loop !17

vector.body.4:                                    ; preds = %vector.body.3
  %44 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %45 = getelementptr inbounds nuw i8, ptr %0, i64 18
  %wide.load.4 = load <2 x i8>, ptr %44, align 1, !alias.scope !7, !noalias !14
  %wide.load29.4 = load <2 x i8>, ptr %45, align 1, !alias.scope !7, !noalias !14
  %46 = icmp sgt <2 x i8> %wide.load.4, splat (i8 -65)
  %47 = icmp sgt <2 x i8> %wide.load29.4, splat (i8 -65)
  %48 = zext <2 x i1> %46 to <2 x i64>
  %49 = zext <2 x i1> %47 to <2 x i64>
  %50 = add nuw nsw <2 x i64> %41, %48
  %51 = add nuw nsw <2 x i64> %42, %49
  %52 = icmp eq i64 %n.vec, 20
  br i1 %52, label %middle.block, label %vector.body.5, !llvm.loop !17

vector.body.5:                                    ; preds = %vector.body.4
  %53 = getelementptr inbounds nuw i8, ptr %0, i64 20
  %54 = getelementptr inbounds nuw i8, ptr %0, i64 22
  %wide.load.5 = load <2 x i8>, ptr %53, align 1, !alias.scope !7, !noalias !14
  %wide.load29.5 = load <2 x i8>, ptr %54, align 1, !alias.scope !7, !noalias !14
  %55 = icmp sgt <2 x i8> %wide.load.5, splat (i8 -65)
  %56 = icmp sgt <2 x i8> %wide.load29.5, splat (i8 -65)
  %57 = zext <2 x i1> %55 to <2 x i64>
  %58 = zext <2 x i1> %56 to <2 x i64>
  %59 = add nuw nsw <2 x i64> %50, %57
  %60 = add nuw nsw <2 x i64> %51, %58
  %61 = icmp eq i64 %n.vec, 24
  br i1 %61, label %middle.block, label %vector.body.6, !llvm.loop !17

vector.body.6:                                    ; preds = %vector.body.5
  %62 = getelementptr inbounds nuw i8, ptr %0, i64 24
  %63 = getelementptr inbounds nuw i8, ptr %0, i64 26
  %wide.load.6 = load <2 x i8>, ptr %62, align 1, !alias.scope !7, !noalias !14
  %wide.load29.6 = load <2 x i8>, ptr %63, align 1, !alias.scope !7, !noalias !14
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
  %byte.i.i.i.i.i.i.i = load i8, ptr %_37.i.i.i.i, align 1, !alias.scope !7, !noalias !14, !noundef !4
  %_4.i.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i.i, %_0.i.i.i.i.i.i
  %_21.i.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i.i, 1
  %_22.i.i.i.i = icmp eq i64 %_21.i.i.i.i, %1
  br i1 %_22.i.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i, !llvm.loop !20

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i: ; preds = %bb9.i.i.i.i, %middle.block, %bb8.i
  %_0.sroa.0.0.i.i.i.i = phi i64 [ 0, %bb8.i ], [ %70, %middle.block ], [ %_4.0.i.i.i.i.i.i, %bb9.i.i.i.i ]
  %_3.i.i.i.i = icmp ule i64 %_0.sroa.0.0.i.i.i.i, %1
  tail call void @llvm.assume(i1 %_3.i.i.i.i)
  br label %bb7

bb16:                                             ; preds = %bb3
  %71 = getelementptr inbounds nuw i8, ptr %self, i64 22
  %_31 = load i16, ptr %71, align 2, !noundef !4
  %_38 = getelementptr inbounds nuw i8, ptr %0, i64 %1
  %_11 = zext i16 %_31 to i64
  %.not = icmp eq i16 %_31, 0
  br i1 %.not, label %bb5, label %bb1.i

bb7:                                              ; preds = %bb5, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, %bb9.i
  %char_count.sroa.0.0 = phi i64 [ %82, %bb5 ], [ %_0.sroa.0.0.i.i.i.i, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i ], [ %9, %bb9.i ]
  %s.sroa.8.0 = phi i64 [ %iter.sroa.10.1, %bb5 ], [ %1, %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i ], [ %1, %bb9.i ]
  %72 = getelementptr inbounds nuw i8, ptr %self, i64 20
  %_18 = load i16, ptr %72, align 4, !noundef !4
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
  %x.i.i.i = load i8, ptr %_13.i19.i.i8.i, align 1, !noalias !21, !noundef !4
  %_6.i.i.i = icmp sgt i8 %x.i.i.i, -1
  br i1 %_6.i.i.i, label %bb3.i.i.i, label %bb4.i.i.i

bb4.i.i.i:                                        ; preds = %bb14.i.i.i
  %_30.i.i.i = and i8 %x.i.i.i, 31
  %init.i.i.i = zext nneg i8 %_30.i.i.i to i32
  %_6.i3.i.i.i = icmp ne ptr %_13.i.i.i.i, %_38
  tail call void @llvm.assume(i1 %_6.i3.i.i.i)
  %_13.i5.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i19.i.i8.i, i64 2
  %y.i.i.i = load i8, ptr %_13.i.i.i.i, align 1, !noalias !21, !noundef !4
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
  %z.i.i.i = load i8, ptr %_13.i5.i.i.i, align 1, !noalias !21, !noundef !4
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
  %w.i.i.i = load i8, ptr %_13.i12.i.i.i, align 1, !noalias !21, !noundef !4
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
  %_28.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %83 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_28.1 = load ptr, ptr %83, align 8, !nonnull !4, !align !6, !noundef !4
  %84 = getelementptr inbounds nuw i8, ptr %_28.1, i64 24
  %85 = load ptr, ptr %84, align 8, !invariant.load !4, !nonnull !4
  %86 = tail call noundef zeroext i1 %85(ptr noundef nonnull align 1 %_28.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %s.sroa.8.0) #30
  br label %bb15

bb8:                                              ; preds = %bb7
  %_23 = trunc nuw i64 %char_count.sroa.0.0 to i16
  %_21 = sub i16 %_18, %_23
  tail call void @llvm.experimental.noalias.scope.decl(metadata !28)
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
  %_13.0.i = load ptr, ptr %self, align 8, !alias.scope !28, !nonnull !4, !align !5
  %90 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i = load ptr, ptr %90, align 8, !alias.scope !28, !nonnull !4, !align !6
  %91 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 32
  br label %bb7.i

bb7.i:                                            ; preds = %bb17.i, %bb6.i
  %iter.sroa.0.0.i = phi i16 [ 0, %bb6.i ], [ %_17.i, %bb17.i ]
  %exitcond.not.i = icmp eq i16 %iter.sroa.0.0.i, %padding_left.sroa.0.0.i
  br i1 %exitcond.not.i, label %bb27, label %bb17.i

bb17.i:                                           ; preds = %bb7.i
  %_17.i = add i16 %iter.sroa.0.0.i, 1
  %92 = load ptr, ptr %91, align 8, !invariant.load !4, !noalias !28, !nonnull !4
  %_10.i = tail call noundef zeroext i1 %92(ptr noundef nonnull align 1 %_13.0.i, i32 noundef %_2.i11.i) #30, !noalias !28
  br i1 %_10.i, label %bb15, label %bb7.i

bb27:                                             ; preds = %bb7.i
  %_12.i = sub i16 %_21, %padding_left.sroa.0.0.i
  %93 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 24
  %94 = load ptr, ptr %93, align 8, !invariant.load !4, !nonnull !4
  %_25 = tail call noundef zeroext i1 %94(ptr noundef nonnull align 1 %_13.0.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %s.sroa.8.0) #30
  br i1 %_25, label %bb15, label %bb1.i18

bb1.i18:                                          ; preds = %bb8.i21, %bb27
  %iter.sroa.0.0.i19 = phi i16 [ %_8.i, %bb8.i21 ], [ 0, %bb27 ]
  %exitcond.not.i20 = icmp eq i16 %iter.sroa.0.0.i19, %_12.i
  br i1 %exitcond.not.i20, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb8.i21

bb8.i21:                                          ; preds = %bb1.i18
  %_8.i = add i16 %iter.sroa.0.0.i19, 1
  %95 = load ptr, ptr %91, align 8, !invariant.load !4, !noalias !31, !nonnull !4
  %_4.i = tail call noundef zeroext i1 %95(ptr noundef nonnull align 1 %_13.0.i, i32 noundef range(i32 0, 1114112) %_2.i11.i) #30, !noalias !31
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
  br i1 %0, label %bb19, label %bb21, !prof !34

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
  %wide.load = load <2 x i8>, ptr %2, align 1, !alias.scope !35, !noalias !42
  %wide.load73 = load <2 x i8>, ptr %3, align 1, !alias.scope !35, !noalias !42
  %4 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %5 = icmp sgt <2 x i8> %wide.load73, splat (i8 -65)
  %6 = zext <2 x i1> %4 to <2 x i64>
  %7 = zext <2 x i1> %5 to <2 x i64>
  %8 = add <2 x i64> %vec.phi, %6
  %9 = add <2 x i64> %vec.phi72, %7
  %index.next = add nuw i64 %index, 4
  %10 = icmp eq i64 %index.next, %n.vec
  br i1 %10, label %middle.block, label %vector.body, !llvm.loop !45

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
  %byte.i.i.i.i.i.i = load i8, ptr %_37.i.i.i, align 1, !alias.scope !35, !noalias !42, !noundef !4
  %_4.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i, %_0.i.i.i.i.i
  %_21.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i, 1
  %_22.i.i.i = icmp eq i64 %_21.i.i.i, %byte_offset.i.i
  br i1 %_22.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit, label %bb9.i.i.i, !llvm.loop !46

_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit: ; preds = %bb9.i.i.i, %middle.block, %bb21
  %_0.sroa.0.0.i.i.i = phi i64 [ 0, %bb21 ], [ %11, %middle.block ], [ %_4.0.i.i.i.i.i, %bb9.i.i.i ]
  %_3.i.i.i = icmp ule i64 %_0.sroa.0.0.i.i.i, %byte_offset.i.i
  tail call void @llvm.assume(i1 %_3.i.i.i)
  %12 = icmp samesign eq i64 %ts_len.i.i, 0
  br i1 %12, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17

bb9.i.i.i17:                                      ; preds = %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit
  %byte.i.i.i.i.i.i21 = load i8, ptr %_19.i, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22 = icmp sgt i8 %byte.i.i.i.i.i.i21, -65
  %_0.i.i.i.i.i23 = zext i1 %_4.i.i.i.i.i.i22 to i64
  %_22.i.i.i26 = icmp eq i64 %ts_len.i.i, 1
  br i1 %_22.i.i.i26, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.1

bb9.i.i.i17.1:                                    ; preds = %bb9.i.i.i17
  %_37.i.i.i20.1 = getelementptr inbounds nuw i8, ptr %_19.i, i64 1
  %byte.i.i.i.i.i.i21.1 = load i8, ptr %_37.i.i.i20.1, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22.1 = icmp sgt i8 %byte.i.i.i.i.i.i21.1, -65
  %_0.i.i.i.i.i23.1 = zext i1 %_4.i.i.i.i.i.i22.1 to i64
  %_4.0.i.i.i.i.i24.1 = add nuw nsw i64 %_0.i.i.i.i.i23, %_0.i.i.i.i.i23.1
  %_22.i.i.i26.1 = icmp eq i64 %ts_len.i.i, 2
  br i1 %_22.i.i.i26.1, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.2

bb9.i.i.i17.2:                                    ; preds = %bb9.i.i.i17.1
  %_37.i.i.i20.2 = getelementptr inbounds nuw i8, ptr %_19.i, i64 2
  %byte.i.i.i.i.i.i21.2 = load i8, ptr %_37.i.i.i20.2, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22.2 = icmp sgt i8 %byte.i.i.i.i.i.i21.2, -65
  %_0.i.i.i.i.i23.2 = zext i1 %_4.i.i.i.i.i.i22.2 to i64
  %_4.0.i.i.i.i.i24.2 = add nuw nsw i64 %_4.0.i.i.i.i.i24.1, %_0.i.i.i.i.i23.2
  %_22.i.i.i26.2 = icmp eq i64 %ts_len.i.i, 3
  br i1 %_22.i.i.i26.2, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.3

bb9.i.i.i17.3:                                    ; preds = %bb9.i.i.i17.2
  %_37.i.i.i20.3 = getelementptr inbounds nuw i8, ptr %_19.i, i64 3
  %byte.i.i.i.i.i.i21.3 = load i8, ptr %_37.i.i.i20.3, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22.3 = icmp sgt i8 %byte.i.i.i.i.i.i21.3, -65
  %_0.i.i.i.i.i23.3 = zext i1 %_4.i.i.i.i.i.i22.3 to i64
  %_4.0.i.i.i.i.i24.3 = add nuw nsw i64 %_4.0.i.i.i.i.i24.2, %_0.i.i.i.i.i23.3
  %_22.i.i.i26.3 = icmp eq i64 %ts_len.i.i, 4
  br i1 %_22.i.i.i26.3, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.4

bb9.i.i.i17.4:                                    ; preds = %bb9.i.i.i17.3
  %_37.i.i.i20.4 = getelementptr inbounds nuw i8, ptr %_19.i, i64 4
  %byte.i.i.i.i.i.i21.4 = load i8, ptr %_37.i.i.i20.4, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22.4 = icmp sgt i8 %byte.i.i.i.i.i.i21.4, -65
  %_0.i.i.i.i.i23.4 = zext i1 %_4.i.i.i.i.i.i22.4 to i64
  %_4.0.i.i.i.i.i24.4 = add nuw nsw i64 %_4.0.i.i.i.i.i24.3, %_0.i.i.i.i.i23.4
  %_22.i.i.i26.4 = icmp eq i64 %ts_len.i.i, 5
  br i1 %_22.i.i.i26.4, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.5

bb9.i.i.i17.5:                                    ; preds = %bb9.i.i.i17.4
  %_37.i.i.i20.5 = getelementptr inbounds nuw i8, ptr %_19.i, i64 5
  %byte.i.i.i.i.i.i21.5 = load i8, ptr %_37.i.i.i20.5, align 1, !alias.scope !47, !noalias !54, !noundef !4
  %_4.i.i.i.i.i.i22.5 = icmp sgt i8 %byte.i.i.i.i.i.i21.5, -65
  %_0.i.i.i.i.i23.5 = zext i1 %_4.i.i.i.i.i.i22.5 to i64
  %_4.0.i.i.i.i.i24.5 = add nuw nsw i64 %_4.0.i.i.i.i.i24.4, %_0.i.i.i.i.i23.5
  %_22.i.i.i26.5 = icmp eq i64 %ts_len.i.i, 6
  br i1 %_22.i.i.i26.5, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit29, label %bb9.i.i.i17.6

bb9.i.i.i17.6:                                    ; preds = %bb9.i.i.i17.5
  %_37.i.i.i20.6 = getelementptr inbounds nuw i8, ptr %_19.i, i64 6
  %byte.i.i.i.i.i.i21.6 = load i8, ptr %_37.i.i.i20.6, align 1, !alias.scope !47, !noalias !54, !noundef !4
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
  %wide.load107 = load <2 x i8>, ptr %15, align 1, !alias.scope !57, !noalias !64
  %wide.load108 = load <2 x i8>, ptr %16, align 1, !alias.scope !57, !noalias !64
  %17 = icmp sgt <2 x i8> %wide.load107, splat (i8 -65)
  %18 = icmp sgt <2 x i8> %wide.load108, splat (i8 -65)
  %19 = zext <2 x i1> %17 to <2 x i64>
  %20 = zext <2 x i1> %18 to <2 x i64>
  %21 = add <2 x i64> %vec.phi105, %19
  %22 = add <2 x i64> %vec.phi106, %20
  %index.next109 = add nuw i64 %index104, 4
  %23 = icmp eq i64 %index.next109, %n.vec102
  br i1 %23, label %middle.block110, label %vector.body103, !llvm.loop !67

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
  %byte.i.i.i.i.i.i34 = load i8, ptr %_37.i.i.i33, align 1, !alias.scope !57, !noalias !64, !noundef !4
  %_4.i.i.i.i.i.i35 = icmp sgt i8 %byte.i.i.i.i.i.i34, -65
  %_0.i.i.i.i.i36 = zext i1 %_4.i.i.i.i.i.i35 to i64
  %_4.0.i.i.i.i.i37 = add i64 %init.sroa.0.0.i.i.i32, %_0.i.i.i.i.i36
  %_21.i.i.i38 = add nuw nsw i64 %i.sroa.0.0.i.i.i31, 1
  %_22.i.i.i39 = icmp eq i64 %_21.i.i.i38, %s.1
  br i1 %_22.i.i.i39, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit42, label %bb9.i.i.i30, !llvm.loop !68

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
  br i1 %80, label %middle.block93, label %vector.body79, !llvm.loop !69

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
  %word = load i64, ptr %iter.sroa.0.054, align 8, !noundef !4
  %_86 = xor i64 %word, -1
  %_85 = lshr i64 %_86, 7
  %_87 = lshr i64 %word, 6
  %_84 = or i64 %_85, %_87
  %_24 = and i64 %_84, 72340172838076673
  %82 = add i64 %_24, %counts.sroa.0.055
  %iter1.sroa.0.0.ptr.1 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 8
  %word.1 = load i64, ptr %iter1.sroa.0.0.ptr.1, align 8, !noundef !4
  %_86.1 = xor i64 %word.1, -1
  %_85.1 = lshr i64 %_86.1, 7
  %_87.1 = lshr i64 %word.1, 6
  %_84.1 = or i64 %_85.1, %_87.1
  %_24.1 = and i64 %_84.1, 72340172838076673
  %83 = add i64 %_24.1, %82
  %iter1.sroa.0.0.ptr.2 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 16
  %word.2 = load i64, ptr %iter1.sroa.0.0.ptr.2, align 8, !noundef !4
  %_86.2 = xor i64 %word.2, -1
  %_85.2 = lshr i64 %_86.2, 7
  %_87.2 = lshr i64 %word.2, 6
  %_84.2 = or i64 %_85.2, %_87.2
  %_24.2 = and i64 %_84.2, 72340172838076673
  %84 = add i64 %_24.2, %83
  %iter1.sroa.0.0.ptr.3 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 24
  %word.3 = load i64, ptr %iter1.sroa.0.0.ptr.3, align 8, !noundef !4
  %_86.3 = xor i64 %word.3, -1
  %_85.3 = lshr i64 %_86.3, 7
  %_87.3 = lshr i64 %word.3, 6
  %_84.3 = or i64 %_85.3, %_87.3
  %_24.3 = and i64 %_84.3, 72340172838076673
  %85 = add i64 %_24.3, %84
  %_60 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.054, i64 32
  %_54 = icmp eq ptr %_60, %_48
  br i1 %_54, label %bb28, label %bb29, !llvm.loop !70

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
  %word4 = load i64, ptr %_23.i.le, align 8, !noundef !4
  %_107 = xor i64 %word4, -1
  %_106 = lshr i64 %_107, 7
  %_108 = lshr i64 %word4, 6
  %_105 = or i64 %_106, %_108
  %_30 = and i64 %_105, 72340172838076673
  %_97 = icmp eq i64 %_24.i, 1
  br i1 %_97, label %bb32, label %bb33.1

bb33.1:                                           ; preds = %bb33.preheader
  %_103 = getelementptr inbounds nuw i8, ptr %_23.i.le, i64 8
  %word4.1 = load i64, ptr %_103, align 8, !noundef !4
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
  %word4.2 = load i64, ptr %_103.1, align 8, !noundef !4
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
declare i64 @llvm.vector.reduce.add.v2i64(<2 x i64>) #6

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
  %_36 = load i32, ptr %1, align 8, !noundef !4
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
  %5 = tail call noundef i64 @_RNvNtNtCscliFh4jUES5_4core3str5count14do_count_chars(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %prefix.0, i64 noundef %prefix.1) #22
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
  %wide.load = load <2 x i8>, ptr %prefix.0, align 1, !alias.scope !71, !noalias !78
  %wide.load68 = load <2 x i8>, ptr %7, align 1, !alias.scope !71, !noalias !78
  %8 = icmp sgt <2 x i8> %wide.load, splat (i8 -65)
  %9 = icmp sgt <2 x i8> %wide.load68, splat (i8 -65)
  %10 = zext <2 x i1> %8 to <2 x i64>
  %11 = zext <2 x i1> %9 to <2 x i64>
  %12 = icmp eq i64 %n.vec, 4
  br i1 %12, label %middle.block, label %vector.body.1, !llvm.loop !81

vector.body.1:                                    ; preds = %vector.ph
  %13 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 4
  %14 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 6
  %wide.load.1 = load <2 x i8>, ptr %13, align 1, !alias.scope !71, !noalias !78
  %wide.load68.1 = load <2 x i8>, ptr %14, align 1, !alias.scope !71, !noalias !78
  %15 = icmp sgt <2 x i8> %wide.load.1, splat (i8 -65)
  %16 = icmp sgt <2 x i8> %wide.load68.1, splat (i8 -65)
  %17 = zext <2 x i1> %15 to <2 x i64>
  %18 = zext <2 x i1> %16 to <2 x i64>
  %19 = add nuw nsw <2 x i64> %10, %17
  %20 = add nuw nsw <2 x i64> %11, %18
  %21 = icmp eq i64 %n.vec, 8
  br i1 %21, label %middle.block, label %vector.body.2, !llvm.loop !81

vector.body.2:                                    ; preds = %vector.body.1
  %22 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 8
  %23 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 10
  %wide.load.2 = load <2 x i8>, ptr %22, align 1, !alias.scope !71, !noalias !78
  %wide.load68.2 = load <2 x i8>, ptr %23, align 1, !alias.scope !71, !noalias !78
  %24 = icmp sgt <2 x i8> %wide.load.2, splat (i8 -65)
  %25 = icmp sgt <2 x i8> %wide.load68.2, splat (i8 -65)
  %26 = zext <2 x i1> %24 to <2 x i64>
  %27 = zext <2 x i1> %25 to <2 x i64>
  %28 = add nuw nsw <2 x i64> %19, %26
  %29 = add nuw nsw <2 x i64> %20, %27
  %30 = icmp eq i64 %n.vec, 12
  br i1 %30, label %middle.block, label %vector.body.3, !llvm.loop !81

vector.body.3:                                    ; preds = %vector.body.2
  %31 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 12
  %32 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 14
  %wide.load.3 = load <2 x i8>, ptr %31, align 1, !alias.scope !71, !noalias !78
  %wide.load68.3 = load <2 x i8>, ptr %32, align 1, !alias.scope !71, !noalias !78
  %33 = icmp sgt <2 x i8> %wide.load.3, splat (i8 -65)
  %34 = icmp sgt <2 x i8> %wide.load68.3, splat (i8 -65)
  %35 = zext <2 x i1> %33 to <2 x i64>
  %36 = zext <2 x i1> %34 to <2 x i64>
  %37 = add nuw nsw <2 x i64> %28, %35
  %38 = add nuw nsw <2 x i64> %29, %36
  %39 = icmp eq i64 %n.vec, 16
  br i1 %39, label %middle.block, label %vector.body.4, !llvm.loop !81

vector.body.4:                                    ; preds = %vector.body.3
  %40 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 16
  %41 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 18
  %wide.load.4 = load <2 x i8>, ptr %40, align 1, !alias.scope !71, !noalias !78
  %wide.load68.4 = load <2 x i8>, ptr %41, align 1, !alias.scope !71, !noalias !78
  %42 = icmp sgt <2 x i8> %wide.load.4, splat (i8 -65)
  %43 = icmp sgt <2 x i8> %wide.load68.4, splat (i8 -65)
  %44 = zext <2 x i1> %42 to <2 x i64>
  %45 = zext <2 x i1> %43 to <2 x i64>
  %46 = add nuw nsw <2 x i64> %37, %44
  %47 = add nuw nsw <2 x i64> %38, %45
  %48 = icmp eq i64 %n.vec, 20
  br i1 %48, label %middle.block, label %vector.body.5, !llvm.loop !81

vector.body.5:                                    ; preds = %vector.body.4
  %49 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 20
  %50 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 22
  %wide.load.5 = load <2 x i8>, ptr %49, align 1, !alias.scope !71, !noalias !78
  %wide.load68.5 = load <2 x i8>, ptr %50, align 1, !alias.scope !71, !noalias !78
  %51 = icmp sgt <2 x i8> %wide.load.5, splat (i8 -65)
  %52 = icmp sgt <2 x i8> %wide.load68.5, splat (i8 -65)
  %53 = zext <2 x i1> %51 to <2 x i64>
  %54 = zext <2 x i1> %52 to <2 x i64>
  %55 = add nuw nsw <2 x i64> %46, %53
  %56 = add nuw nsw <2 x i64> %47, %54
  %57 = icmp eq i64 %n.vec, 24
  br i1 %57, label %middle.block, label %vector.body.6, !llvm.loop !81

vector.body.6:                                    ; preds = %vector.body.5
  %58 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 24
  %59 = getelementptr inbounds nuw i8, ptr %prefix.0, i64 26
  %wide.load.6 = load <2 x i8>, ptr %58, align 1, !alias.scope !71, !noalias !78
  %wide.load68.6 = load <2 x i8>, ptr %59, align 1, !alias.scope !71, !noalias !78
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
  %byte.i.i.i.i.i.i.i = load i8, ptr %_37.i.i.i.i, align 1, !alias.scope !71, !noalias !78, !noundef !4
  %_4.i.i.i.i.i.i.i = icmp sgt i8 %byte.i.i.i.i.i.i.i, -65
  %_0.i.i.i.i.i.i = zext i1 %_4.i.i.i.i.i.i.i to i64
  %_4.0.i.i.i.i.i.i = add i64 %init.sroa.0.0.i.i.i.i, %_0.i.i.i.i.i.i
  %_21.i.i.i.i = add nuw nsw i64 %i.sroa.0.0.i.i.i.i, 1
  %_22.i.i.i.i = icmp eq i64 %_21.i.i.i.i, %prefix.1
  br i1 %_22.i.i.i.i, label %_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case.exit.i, label %bb9.i.i.i.i, !llvm.loop !82

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
  %min = load i16, ptr %68, align 4, !noundef !4
  %_13 = zext i16 %min to i64
  %_11.not = icmp ult i64 %width.sroa.0.1, %_13
  br i1 %_11.not, label %bb14, label %bb11

bb14:                                             ; preds = %bb10
  %_49 = and i32 %_38, 16777216
  %69 = icmp eq i32 %_49, 0
  br i1 %69, label %bb20, label %bb15

bb11:                                             ; preds = %bb10
  %_14 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #31
  br i1 %_14, label %bb31, label %bb33

bb20:                                             ; preds = %bb14
  %_27 = trunc nuw i64 %width.sroa.0.1 to i16
  %_26 = sub i16 %min, %_27
  tail call void @llvm.experimental.noalias.scope.decl(metadata !83)
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
  %_13.0.i = load ptr, ptr %self, align 8, !alias.scope !83, !nonnull !4, !align !5
  %73 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i = load ptr, ptr %73, align 8, !alias.scope !83, !nonnull !4, !align !6
  %74 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 32
  br label %bb7.i

bb7.i:                                            ; preds = %bb17.i, %bb6.i
  %iter.sroa.0.0.i = phi i16 [ 0, %bb6.i ], [ %_17.i, %bb17.i ]
  %exitcond.not.i = icmp eq i16 %iter.sroa.0.0.i, %padding_left.sroa.0.0.i
  br i1 %exitcond.not.i, label %bb43, label %bb17.i

bb17.i:                                           ; preds = %bb7.i
  %_17.i = add i16 %iter.sroa.0.0.i, 1
  %75 = load ptr, ptr %74, align 8, !invariant.load !4, !noalias !83, !nonnull !4
  %_10.i = tail call noundef zeroext i1 %75(ptr noundef nonnull align 1 %_13.0.i, i32 noundef %_2.i11.i) #30, !noalias !83
  br i1 %_10.i, label %bb31, label %bb7.i

bb15:                                             ; preds = %bb14
  %old_options.sroa.0.0.copyload = load i64, ptr %3, align 8
  %76 = trunc i64 %old_options.sroa.0.0.copyload to i32
  %_51 = and i32 %76, -1612709888
  %77 = or disjoint i32 %_51, 536870960
  store i32 %77, ptr %3, align 8
  %_16 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #31
  br i1 %_16, label %bb31, label %bb35

bb43:                                             ; preds = %bb7.i
  %_12.i = sub i16 %_26, %padding_left.sroa.0.0.i
  %_29 = tail call fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef align 8 dereferenceable(24) %self, i32 noundef %sign.sroa.0.0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %prefix.sroa.0.0, i64 %prefix.1) #31
  br i1 %_29, label %bb31, label %bb45

bb45:                                             ; preds = %bb43
  %78 = getelementptr inbounds nuw i8, ptr %_13.1.i, i64 24
  %79 = load ptr, ptr %78, align 8, !invariant.load !4, !nonnull !4
  %_30 = tail call noundef zeroext i1 %79(ptr noundef nonnull align 1 %_13.0.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #30
  br i1 %_30, label %bb31, label %bb1.i

bb1.i:                                            ; preds = %bb8.i24, %bb45
  %iter.sroa.0.0.i22 = phi i16 [ %_8.i, %bb8.i24 ], [ 0, %bb45 ]
  %exitcond.not.i23 = icmp eq i16 %iter.sroa.0.0.i22, %_12.i
  br i1 %exitcond.not.i23, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb8.i24

bb8.i24:                                          ; preds = %bb1.i
  %_8.i = add i16 %iter.sroa.0.0.i22, 1
  %80 = load ptr, ptr %74, align 8, !invariant.load !4, !noalias !86, !nonnull !4
  %_4.i = tail call noundef zeroext i1 %80(ptr noundef nonnull align 1 %_13.0.i, i32 noundef range(i32 0, 1114112) %_2.i11.i) #30, !noalias !86
  br i1 %_4.i, label %_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit, label %bb1.i

_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write.exit: ; preds = %bb8.i24, %bb1.i
  %iter.sroa.0.0.i22.lcssa = phi i16 [ %_12.i, %bb1.i ], [ %iter.sroa.0.0.i22, %bb8.i24 ]
  %_7.i = icmp ult i16 %iter.sroa.0.0.i22.lcssa, %_12.i
  br label %bb31

bb35:                                             ; preds = %bb15
  %_20 = trunc nuw i64 %width.sroa.0.1 to i16
  %_19 = sub i16 %min, %_20
  tail call void @llvm.experimental.noalias.scope.decl(metadata !89)
  %_13.0.i31 = load ptr, ptr %self, align 8, !alias.scope !89, !nonnull !4, !align !5
  %81 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_13.1.i32 = load ptr, ptr %81, align 8, !alias.scope !89, !nonnull !4, !align !6
  %82 = getelementptr inbounds nuw i8, ptr %_13.1.i32, i64 32
  br label %bb7.i33

bb7.i33:                                          ; preds = %bb17.i36, %bb35
  %iter.sroa.0.0.i34 = phi i16 [ 0, %bb35 ], [ %_17.i37, %bb17.i36 ]
  %exitcond.not.i35 = icmp eq i16 %iter.sroa.0.0.i34, %_19
  br i1 %exitcond.not.i35, label %bb37, label %bb17.i36

bb17.i36:                                         ; preds = %bb7.i33
  %_17.i37 = add i16 %iter.sroa.0.0.i34, 1
  %83 = load ptr, ptr %82, align 8, !invariant.load !4, !noalias !89, !nonnull !4
  %_10.i38 = tail call noundef zeroext i1 %83(ptr noundef nonnull align 1 %_13.0.i31, i32 noundef 48) #30, !noalias !89
  br i1 %_10.i38, label %bb31, label %bb7.i33

bb37:                                             ; preds = %bb7.i33
  %84 = getelementptr inbounds nuw i8, ptr %_13.1.i32, i64 24
  %85 = load ptr, ptr %84, align 8, !invariant.load !4, !nonnull !4
  %_22 = tail call noundef zeroext i1 %85(ptr noundef nonnull align 1 %_13.0.i31, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #30
  br i1 %_22, label %bb31, label %bb41

bb41:                                             ; preds = %bb37
  store i64 %old_options.sroa.0.0.copyload, ptr %3, align 8
  br label %bb31

bb33:                                             ; preds = %bb11
  %_31.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %86 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_31.1 = load ptr, ptr %86, align 8, !nonnull !4, !align !6, !noundef !4
  %87 = getelementptr inbounds nuw i8, ptr %_31.1, i64 24
  %88 = load ptr, ptr %87, align 8, !invariant.load !4, !nonnull !4
  %89 = tail call noundef zeroext i1 %88(ptr noundef nonnull align 1 %_31.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %buf.0, i64 noundef %buf.1) #30
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
  %_63 = load i8, ptr %7, align 1, !noundef !4
  store i8 %_63, ptr %_60, align 1
  %_68 = add nsw i64 %offset.sroa.0.0.lcssa, -1
  %_70 = icmp ult i64 %_68, %buf.1
  br i1 %_70, label %bb19, label %panic2

panic:                                            ; preds = %bb14
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %2, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

panic2:                                           ; preds = %bb16
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_68, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

bb19:                                             ; preds = %bb16
  %_67 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_68
  %8 = getelementptr inbounds nuw i8, ptr %7, i64 1
  %_71 = load i8, ptr %8, align 1, !noundef !4
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
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %9, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

bb25:                                             ; preds = %bb23
  %_88 = shl nuw nsw i64 %remain.sroa.0.1, 1
  %_82 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %9
  %10 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_88
  %11 = getelementptr inbounds nuw i8, ptr %10, i64 1
  %_85 = load i8, ptr %11, align 1, !noundef !4
  store i8 %_85, ptr %_82, align 1
  br label %bb26

bb5:                                              ; preds = %bb2
  %12 = shl nuw nsw i16 %_1945, 1
  %_27 = zext nneg i16 %12 to i64
  %_22 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %0
  %13 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_27
  %_25 = load i8, ptr %13, align 1, !noundef !4
  store i8 %_25, ptr %_22, align 1
  %_30 = add nsw i64 %offset.sroa.0.063, -3
  %_32 = icmp ult i64 %_30, %buf.1
  br i1 %_32, label %bb8, label %panic10

panic8:                                           ; preds = %bb2
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %0, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

panic10:                                          ; preds = %bb5
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_30, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

bb8:                                              ; preds = %bb5
  %_29 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_30
  %14 = getelementptr inbounds nuw i8, ptr %13, i64 1
  %_33 = load i8, ptr %14, align 1, !noundef !4
  store i8 %_33, ptr %_29, align 1
  %_37 = add nsw i64 %offset.sroa.0.063, -2
  %_39 = icmp ult i64 %_37, %buf.1
  br i1 %_39, label %bb9, label %panic12

bb9:                                              ; preds = %bb8
  %15 = shl nuw nsw i16 %_2144, 1
  %_41 = zext nneg i16 %15 to i64
  %_36 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_37
  %16 = getelementptr inbounds nuw i8, ptr @alloc_7ef6fe6e5749e409bd35c794f395407d, i64 %_41
  %_40 = load i8, ptr %16, align 1, !noundef !4
  store i8 %_40, ptr %_36, align 1
  %_44 = add nsw i64 %offset.sroa.0.063, -1
  %_46 = icmp ult i64 %_44, %buf.1
  br i1 %_46, label %bb12, label %panic14

panic12:                                          ; preds = %bb8
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_37, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

panic14:                                          ; preds = %bb9
  tail call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %_44, i64 noundef %buf.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.9) #29
  unreachable

bb12:                                             ; preds = %bb9
  %_43 = getelementptr inbounds nuw i8, ptr %buf.0, i64 %_44
  %17 = getelementptr inbounds nuw i8, ptr %16, i64 1
  %_47 = load i8, ptr %17, align 1, !noundef !4
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_a434a7e153ac922489f2ed192aeda5a5, ptr noundef nonnull %args, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %2) #29
  unreachable
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvXsd_NtNtNtCscliFh4jUES5_4core3fmt3num3impyNtB9_7Display3fmt(ptr noalias noundef readonly align 8 captures(none) dereferenceable(8) %self, ptr noalias noundef align 8 captures(none) dereferenceable(24) %f) unnamed_addr #1 {
start:
  %buf = alloca [20 x i8], align 1
  call void @llvm.lifetime.start.p0(i64 20, ptr nonnull %buf)
  %_5 = load i64, ptr %self, align 8, !noundef !4
  %offset.i = call fastcc noundef i64 @_RNvMsf_NtNtNtCscliFh4jUES5_4core3fmt3num3impy10__fmt_inner(i64 noundef %_5, ptr noalias noundef nonnull align 1 %buf, i64 noundef 20) #22
  %_8.i.i = sub nuw nsw i64 20, %offset.i
  %_10.i.i = getelementptr inbounds nuw i8, ptr %buf, i64 %offset.i
  %_0 = call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter12pad_integral(ptr noalias noundef nonnull align 8 dereferenceable(24) %f, i1 noundef zeroext true, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) inttoptr (i64 1 to ptr), i64 noundef 0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_10.i.i, i64 noundef %_8.i.i) #22
  call void @llvm.lifetime.end.p0(i64 20, ptr nonnull %buf)
  ret i1 %_0
}

; Function Attrs: noinline nounwind nonlazybind uwtable
define internal fastcc noundef zeroext i1 @_RNvNvMsa_NtCscliFh4jUES5_4core3fmtNtB7_9Formatter12pad_integral12write_prefix(ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(24) %f, i32 noundef range(i32 43, 1114113) %0, ptr noalias noundef readonly align 1 captures(address, read_provenance) %1, i64 %2) unnamed_addr #12 {
start:
  %.not = icmp eq i32 %0, 1114112
  br i1 %.not, label %bb4, label %bb1

bb1:                                              ; preds = %start
  %_9.0 = load ptr, ptr %f, align 8, !nonnull !4, !align !5, !noundef !4
  %3 = getelementptr inbounds nuw i8, ptr %f, i64 8
  %_9.1 = load ptr, ptr %3, align 8, !nonnull !4, !align !6, !noundef !4
  %4 = getelementptr inbounds nuw i8, ptr %_9.1, i64 32
  %5 = load ptr, ptr %4, align 8, !invariant.load !4, !nonnull !4
  %_6 = tail call noundef zeroext i1 %5(ptr noundef nonnull align 1 %_9.0, i32 noundef %0) #30
  br i1 %_6, label %bb7, label %bb4

bb4:                                              ; preds = %bb1, %start
  %.not3 = icmp eq ptr %1, null
  br i1 %.not3, label %bb7, label %bb5

bb7:                                              ; preds = %bb5, %bb4, %bb1
  %_0.sroa.0.0 = phi i1 [ %9, %bb5 ], [ true, %bb1 ], [ false, %bb4 ]
  ret i1 %_0.sroa.0.0

bb5:                                              ; preds = %bb4
  %_10.0 = load ptr, ptr %f, align 8, !nonnull !4, !align !5, !noundef !4
  %6 = getelementptr inbounds nuw i8, ptr %f, i64 8
  %_10.1 = load ptr, ptr %6, align 8, !nonnull !4, !align !6, !noundef !4
  %7 = getelementptr inbounds nuw i8, ptr %_10.1, i64 24
  %8 = load ptr, ptr %7, align 8, !invariant.load !4, !nonnull !4
  %9 = tail call noundef zeroext i1 %8(ptr noundef nonnull align 1 %_10.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %1, i64 noundef %2) #30
  br label %bb7
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #13

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %start1, i64 noundef %end, i64 noundef %len, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) unnamed_addr #7 {
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_9fe2a66cc3026f503dfb4e2696ddcf9c, ptr noundef nonnull %_17, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #29
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_f60af4dda577965ed7990eef71c78b71, ptr noundef nonnull %_37, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #29
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_f60af4dda577965ed7990eef71c78b71, ptr noundef nonnull %_77, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #29
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_bfe1e99c4b1e5a3c7be66967fd889b22, ptr noundef nonnull %_57, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %0) #29
  unreachable
}

; Function Attrs: nofree norecurse nosync nounwind nonlazybind memory(argmem: readwrite, inaccessiblemem: readwrite) uwtable
define internal void @_RNvNtNtCscliFh4jUES5_4core3str8converts9from_utf8(ptr dead_on_unwind noalias noundef writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %v.0, i64 noundef range(i64 0, -9223372036854775808) %v.1) unnamed_addr #14 {
start:
  tail call void @llvm.experimental.noalias.scope.decl(metadata !92)
  %blocks_end.sroa.0.0.i = tail call i64 @llvm.usub.sat.i64(i64 %v.1, i64 15)
  %addr.i.i = ptrtoint ptr %v.0 to i64
  %_9.i.i = add i64 %addr.i.i, 7
  %aligned_address.i.i = and i64 %_9.i.i, -8
  %byte_offset.i.i = sub i64 %aligned_address.i.i, %addr.i.i
  %_12.i.i = icmp ult i64 %byte_offset.i.i, 8
  tail call void @llvm.assume(i1 %_12.i.i), !noalias !95
  %_8.i14.not = icmp eq i64 %v.1, 0
  br i1 %_8.i14.not, label %bb4, label %bb6.i

bb6.i:                                            ; preds = %bb81.i, %start
  %index.sroa.0.0.i15 = phi i64 [ %index.sroa.0.3.i, %bb81.i ], [ 0, %start ]
  %0 = getelementptr inbounds nuw i8, ptr %v.0, i64 %index.sroa.0.0.i15
  %first.i = load i8, ptr %0, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_14.i = icmp slt i8 %first.i, 0
  br i1 %_14.i, label %bb90.i, label %bb64.i

bb79.i:                                           ; preds = %bb64.i
  %1 = add nuw nsw i64 %index.sroa.0.0.i15, 1
  br label %bb81.i

bb64.i:                                           ; preds = %bb6.i
  %_88.i = sub nsw i64 %byte_offset.i.i, %index.sroa.0.0.i15
  %_114.i = and i64 %_88.i, 7
  %2 = icmp eq i64 %_114.i, 0
  br i1 %2, label %bb66.i.preheader, label %bb79.i

bb66.i.preheader:                                 ; preds = %bb64.i
  %_91.i7 = icmp ult i64 %index.sroa.0.0.i15, %blocks_end.sroa.0.0.i
  br i1 %_91.i7, label %bb67.i, label %bb70.i

bb67.i:                                           ; preds = %bb68.i, %bb66.i.preheader
  %index.sroa.0.1.i8 = phi i64 [ %5, %bb68.i ], [ %index.sroa.0.0.i15, %bb66.i.preheader ]
  %_95.i = getelementptr inbounds nuw i8, ptr %v.0, i64 %index.sroa.0.1.i8
  %_98.i = load i64, ptr %_95.i, align 8, !alias.scope !92, !noalias !95, !noundef !4
  %_101.i = getelementptr inbounds nuw i8, ptr %_95.i, i64 8
  %_100.i = load i64, ptr %_101.i, align 8, !alias.scope !92, !noalias !95, !noundef !4
  %3 = or i64 %_100.i, %_98.i
  %4 = and i64 %3, -9187201950435737472
  %brmerge.not.i = icmp eq i64 %4, 0
  br i1 %brmerge.not.i, label %bb68.i, label %bb70.i

bb70.i:                                           ; preds = %bb68.i, %bb67.i, %bb66.i.preheader
  %index.sroa.0.1.i.lcssa = phi i64 [ %index.sroa.0.0.i15, %bb66.i.preheader ], [ %index.sroa.0.1.i8, %bb67.i ], [ %5, %bb68.i ]
  %_102.i10 = icmp ult i64 %index.sroa.0.1.i.lcssa, %v.1
  br i1 %_102.i10, label %bb73.i, label %bb81.i

bb68.i:                                           ; preds = %bb67.i
  %5 = add nuw nsw i64 %index.sroa.0.1.i8, 16
  %_91.i = icmp ult i64 %5, %blocks_end.sroa.0.0.i
  br i1 %_91.i, label %bb67.i, label %bb70.i

bb73.i:                                           ; preds = %bb74.i, %bb70.i
  %index.sroa.0.2.i11 = phi i64 [ %7, %bb74.i ], [ %index.sroa.0.1.i.lcssa, %bb70.i ]
  %6 = getelementptr inbounds nuw i8, ptr %v.0, i64 %index.sroa.0.2.i11
  %_105.i = load i8, ptr %6, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_104.i = icmp sgt i8 %_105.i, -1
  br i1 %_104.i, label %bb74.i, label %bb81.i

bb74.i:                                           ; preds = %bb73.i
  %7 = add i64 %index.sroa.0.2.i11, 1
  %exitcond.not = icmp eq i64 %7, %v.1
  br i1 %exitcond.not, label %bb4, label %bb73.i

bb81.i:                                           ; preds = %bb62.i, %bb73.i, %bb70.i, %bb79.i
  %index.sroa.0.3.i = phi i64 [ %13, %bb62.i ], [ %1, %bb79.i ], [ %index.sroa.0.1.i.lcssa, %bb70.i ], [ %index.sroa.0.2.i11, %bb73.i ]
  %_8.i = icmp ult i64 %index.sroa.0.3.i, %v.1
  br i1 %_8.i, label %bb6.i, label %bb4

bb90.i:                                           ; preds = %bb6.i
  %_112.i = zext i8 %first.i to i64
  %8 = getelementptr inbounds nuw i8, ptr @alloc_db51a71a1b6b25b4224d4dc5277f93e7, i64 %_112.i
  %_110.i = load i8, ptr %8, align 1, !noalias !97, !noundef !4
  switch i8 %_110.i, label %bb3 [
    i8 2, label %bb11.i
    i8 3, label %bb10.i
    i8 4, label %bb9.i
  ]

bb11.i:                                           ; preds = %bb90.i
  %9 = add nuw nsw i64 %index.sroa.0.0.i15, 1
  %_19.not.i = icmp ult i64 %9, %v.1
  br i1 %_19.not.i, label %bb14.i, label %bb3

bb10.i:                                           ; preds = %bb90.i
  %10 = add nuw nsw i64 %index.sroa.0.0.i15, 1
  %_26.not.i = icmp ult i64 %10, %v.1
  br i1 %_26.not.i, label %bb19.i, label %bb3

bb9.i:                                            ; preds = %bb90.i
  %11 = add nuw nsw i64 %index.sroa.0.0.i15, 1
  %_54.not.i = icmp ult i64 %11, %v.1
  br i1 %_54.not.i, label %bb41.i, label %bb3

bb14.i:                                           ; preds = %bb11.i
  %12 = getelementptr inbounds nuw i8, ptr %v.0, i64 %9
  %_18.i = load i8, ptr %12, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_16.i = icmp sgt i8 %_18.i, -65
  br i1 %_16.i, label %bb3, label %bb62.i

bb62.i:                                           ; preds = %bb59.i, %bb36.i, %bb14.i
  %index.sroa.0.4.i = phi i64 [ %9, %bb14.i ], [ %17, %bb36.i ], [ %27, %bb59.i ]
  %13 = add nuw nsw i64 %index.sroa.0.4.i, 1
  br label %bb81.i

bb19.i:                                           ; preds = %bb10.i
  %14 = getelementptr inbounds nuw i8, ptr %v.0, i64 %10
  %_25.i = load i8, ptr %14, align 1, !alias.scope !92, !noalias !95, !noundef !4
  switch i8 %first.i, label %bb23.i [
    i8 -32, label %bb21.i
    i8 -19, label %bb31.i
  ]

bb23.i:                                           ; preds = %bb19.i
  %15 = add nsw i8 %first.i, 31
  %or.cond10.i = icmp ult i8 %15, 12
  br i1 %or.cond10.i, label %bb24.i, label %bb26.i

bb21.i:                                           ; preds = %bb19.i
  %16 = and i8 %_25.i, -32
  %or.cond.i = icmp eq i8 %16, -96
  br i1 %or.cond.i, label %bb33.i, label %bb3

bb31.i:                                           ; preds = %bb19.i
  %or.cond9.i = icmp slt i8 %_25.i, -96
  br i1 %or.cond9.i, label %bb33.i, label %bb3

bb33.i:                                           ; preds = %bb24.i, %bb26.i, %bb31.i, %bb21.i
  %17 = add nuw nsw i64 %index.sroa.0.0.i15, 2
  %_47.not.i = icmp ult i64 %17, %v.1
  br i1 %_47.not.i, label %bb36.i, label %bb3

bb26.i:                                           ; preds = %bb23.i
  %18 = and i8 %first.i, -2
  %or.cond11.i = icmp eq i8 %18, -18
  %19 = icmp slt i8 %_25.i, -64
  %or.cond13.i = and i1 %or.cond11.i, %19
  br i1 %or.cond13.i, label %bb33.i, label %bb3

bb24.i:                                           ; preds = %bb23.i
  %or.cond14.i = icmp slt i8 %_25.i, -64
  br i1 %or.cond14.i, label %bb33.i, label %bb3

bb36.i:                                           ; preds = %bb33.i
  %20 = getelementptr inbounds nuw i8, ptr %v.0, i64 %17
  %_46.i = load i8, ptr %20, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_44.i = icmp sgt i8 %_46.i, -65
  br i1 %_44.i, label %bb3, label %bb62.i

bb41.i:                                           ; preds = %bb9.i
  %21 = getelementptr inbounds nuw i8, ptr %v.0, i64 %11
  %_53.i = load i8, ptr %21, align 1, !alias.scope !92, !noalias !95, !noundef !4
  switch i8 %first.i, label %bb45.i [
    i8 -16, label %bb43.i
    i8 -12, label %bb49.i
  ]

bb45.i:                                           ; preds = %bb41.i
  %22 = add nsw i8 %first.i, 15
  %or.cond17.i = icmp ult i8 %22, 3
  %23 = icmp slt i8 %_53.i, -64
  %or.cond19.i = and i1 %or.cond17.i, %23
  br i1 %or.cond19.i, label %bb51.i, label %bb3

bb43.i:                                           ; preds = %bb41.i
  %24 = add i8 %_53.i, 112
  %or.cond15.i = icmp ult i8 %24, 48
  br i1 %or.cond15.i, label %bb51.i, label %bb3

bb49.i:                                           ; preds = %bb41.i
  %or.cond16.i = icmp slt i8 %_53.i, -112
  br i1 %or.cond16.i, label %bb51.i, label %bb3

bb51.i:                                           ; preds = %bb49.i, %bb43.i, %bb45.i
  %25 = add nuw nsw i64 %index.sroa.0.0.i15, 2
  %_71.not.i = icmp ult i64 %25, %v.1
  br i1 %_71.not.i, label %bb54.i, label %bb3

bb54.i:                                           ; preds = %bb51.i
  %26 = getelementptr inbounds nuw i8, ptr %v.0, i64 %25
  %_70.i = load i8, ptr %26, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_68.i = icmp sgt i8 %_70.i, -65
  br i1 %_68.i, label %bb3, label %bb56.i

bb56.i:                                           ; preds = %bb54.i
  %27 = add nuw nsw i64 %index.sroa.0.0.i15, 3
  %_80.not.i = icmp ult i64 %27, %v.1
  br i1 %_80.not.i, label %bb59.i, label %bb3

bb59.i:                                           ; preds = %bb56.i
  %28 = getelementptr inbounds nuw i8, ptr %v.0, i64 %27
  %_79.i = load i8, ptr %28, align 1, !alias.scope !92, !noalias !95, !noundef !4
  %_77.i = icmp sgt i8 %_79.i, -65
  br i1 %_77.i, label %bb3, label %bb62.i

bb3:                                              ; preds = %bb59.i, %bb56.i, %bb54.i, %bb51.i, %bb49.i, %bb43.i, %bb45.i, %bb36.i, %bb24.i, %bb26.i, %bb33.i, %bb31.i, %bb21.i, %bb14.i, %bb9.i, %bb10.i, %bb11.i, %bb90.i
  %_2.sroa.31.0.ph = phi i8 [ 3, %bb59.i ], [ undef, %bb56.i ], [ 2, %bb54.i ], [ undef, %bb51.i ], [ 1, %bb45.i ], [ 1, %bb43.i ], [ 1, %bb49.i ], [ undef, %bb9.i ], [ 2, %bb36.i ], [ undef, %bb33.i ], [ 1, %bb21.i ], [ 1, %bb31.i ], [ 1, %bb26.i ], [ 1, %bb24.i ], [ undef, %bb10.i ], [ 1, %bb14.i ], [ undef, %bb11.i ], [ 1, %bb90.i ]
  %_2.sroa.16.0.ph = phi i8 [ 1, %bb59.i ], [ 0, %bb56.i ], [ 1, %bb54.i ], [ 0, %bb51.i ], [ 1, %bb45.i ], [ 1, %bb43.i ], [ 1, %bb49.i ], [ 0, %bb9.i ], [ 1, %bb36.i ], [ 0, %bb33.i ], [ 1, %bb21.i ], [ 1, %bb31.i ], [ 1, %bb26.i ], [ 1, %bb24.i ], [ 0, %bb10.i ], [ 1, %bb14.i ], [ 0, %bb11.i ], [ 1, %bb90.i ]
  %29 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %index.sroa.0.0.i15, ptr %29, align 8
  %err.sroa.2.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i8 %_2.sroa.16.0.ph, ptr %err.sroa.2.0..sroa_idx, align 8
  %err.sroa.3.0..sroa_idx = getelementptr inbounds nuw i8, ptr %_0, i64 17
  store i8 %_2.sroa.31.0.ph, ptr %err.sroa.3.0..sroa_idx, align 1
  br label %bb5

bb4:                                              ; preds = %bb81.i, %bb74.i, %start
  %30 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store ptr %v.0, ptr %30, align 8
  %31 = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i64 %v.1, ptr %31, align 8
  br label %bb5

bb5:                                              ; preds = %bb4, %bb3
  %storemerge = phi i64 [ 0, %bb4 ], [ 1, %bb3 ]
  store i64 %storemerge, ptr %_0, align 8
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter9write_str(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %self, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %data.0, i64 noundef %data.1) unnamed_addr #1 {
start:
  %_3.0 = load ptr, ptr %self, align 8, !nonnull !4, !align !5, !noundef !4
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_3.1 = load ptr, ptr %0, align 8, !nonnull !4, !align !6, !noundef !4
  %1 = getelementptr inbounds nuw i8, ptr %_3.1, i64 24
  %2 = load ptr, ptr %1, align 8, !invariant.load !4, !nonnull !4
  %_0 = tail call noundef zeroext i1 %2(ptr noundef nonnull align 1 %_3.0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %data.0, i64 noundef %data.1) #30
  ret i1 %_0
}

; Function Attrs: cold noinline noreturn nounwind nonlazybind uwtable
define internal void @_RNvNtCscliFh4jUES5_4core6result13unwrap_failed(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %0, i64 noundef %1, ptr noundef nonnull align 1 %2, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(32) %3, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) %4) unnamed_addr #7 {
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
  call void @_RNvNtCscliFh4jUES5_4core9panicking9panic_fmt(ptr noundef nonnull @alloc_850f178d3cd0d4307d8d45b841b2c2ad, ptr noundef nonnull %args, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(24) %4) #29
  unreachable
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare { i64, i1 } @llvm.umul.with.overflow.i64(i64, i64) #6

; Function Attrs: nounwind nonlazybind allockind("alloc,uninitialized,aligned") allocsize(0) uwtable
define internal noalias noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc12___rust_alloc(i64 noundef %size, i64 allocalign noundef %align) unnamed_addr #15 {
start:
  %0 = add i64 %align, -1
  %1 = icmp sgt i64 %0, -1
  tail call void @llvm.assume(i1 %1)
  %_0.i = tail call noundef ptr @unir_host_alloc(i64 noundef %size, i64 noundef range(i64 1, -9223372036854775807) %align) #22
  ret ptr %_0.i
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef ptr @unir_host_alloc(i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind allockind("free") uwtable
define internal void @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_dealloc(ptr allocptr noundef %ptr, i64 noundef %size, i64 noundef %align) unnamed_addr #16 {
start:
  %0 = add i64 %align, -1
  %1 = icmp sgt i64 %0, -1
  tail call void @llvm.assume(i1 %1)
  tail call void @unir_host_free(ptr noundef %ptr, i64 noundef %size, i64 noundef range(i64 1, -9223372036854775807) %align) #22
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
declare void @unir_host_free(ptr noundef, i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind allockind("realloc,aligned") allocsize(3) uwtable
define internal noalias noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_realloc(ptr allocptr noundef %ptr, i64 noundef %size, i64 allocalign noundef %align, i64 noundef %new_size) unnamed_addr #17 {
start:
  %0 = add i64 %align, -1
  %1 = icmp sgt i64 %0, -1
  tail call void @llvm.assume(i1 %1)
  %_0.i.i = tail call noundef ptr @unir_host_alloc(i64 noundef %new_size, i64 noundef range(i64 1, -9223372036854775807) %align) #22
  %2 = icmp eq ptr %_0.i.i, null
  br i1 %2, label %_RNvYNtCsdMVUwQQj4R6_9unir_cabi4HostNtNtNtCscliFh4jUES5_4core5alloc6global11GlobalAlloc7reallocB4_.exit, label %bb3.i

bb3.i:                                            ; preds = %start
  %spec.store.select.i = tail call i64 @llvm.umin.i64(i64 %new_size, i64 %size)
  tail call void @llvm.memcpy.p0.p0.i64(ptr nonnull align 1 %_0.i.i, ptr align 1 %ptr, i64 %spec.store.select.i, i1 false)
  tail call void @unir_host_free(ptr noundef %ptr, i64 noundef %size, i64 noundef range(i64 1, -9223372036854775807) %align) #22
  br label %_RNvYNtCsdMVUwQQj4R6_9unir_cabi4HostNtNtNtCscliFh4jUES5_4core5alloc6global11GlobalAlloc7reallocB4_.exit

_RNvYNtCsdMVUwQQj4R6_9unir_cabi4HostNtNtNtCscliFh4jUES5_4core5alloc6global11GlobalAlloc7reallocB4_.exit: ; preds = %bb3.i, %start
  ret ptr %_0.i.i
}

; Function Attrs: cold nofree noreturn nounwind nonlazybind uwtable
define internal void @_RNvCs1Y7DaGC1cwg_7___rustc17rust_begin_unwind(ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %_1) unnamed_addr #18 {
start:
  tail call void @abort() #28
  unreachable
}

; Function Attrs: cold nofree noreturn nounwind nonlazybind uwtable
declare void @abort() unnamed_addr #18

; Function Attrs: nounwind nonlazybind allockind("alloc,zeroed,aligned") allocsize(0) uwtable
define internal noalias noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc19___rust_alloc_zeroed(i64 noundef %size, i64 allocalign noundef %align) unnamed_addr #19 {
start:
  %0 = add i64 %align, -1
  %1 = icmp sgt i64 %0, -1
  tail call void @llvm.assume(i1 %1)
  %_0.i = tail call noundef ptr @unir_host_alloc(i64 noundef %size, i64 noundef range(i64 1, -9223372036854775807) %align) #22
  %2 = icmp eq ptr %_0.i, null
  br i1 %2, label %bb1, label %bb4

bb4:                                              ; preds = %start
  tail call void @llvm.memset.p0.i64(ptr nonnull align 1 %_0.i, i8 0, i64 %size, i1 false)
  br label %bb1

bb1:                                              ; preds = %bb4, %start
  ret ptr %_0.i
}

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
  tail call void @llvm.experimental.noalias.scope.decl(metadata !98)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i)
  %3 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %4 = load i8, ptr %3, align 4, !range !101, !alias.scope !98, !noalias !102, !noundef !4
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb8.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3: ; preds = %start
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  br label %bb6

bb8.i:                                            ; preds = %start
  %_13 = getelementptr inbounds nuw i8, ptr %c, i64 16
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_13, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5, i8 noundef 11, i8 noundef 3, i8 undef) #22, !noalias !105
  %5 = load i8, ptr %err.i, align 8, !range !101, !noalias !106, !noundef !4
  %6 = icmp eq i8 %5, 0
  br i1 %6, label %bb4.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb8.i
  %7 = getelementptr inbounds nuw i8, ptr %err.i, i64 1
  %8 = load i8, ptr %7, align 1, !range !107, !noalias !106, !noundef !4
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
  call void @llvm.experimental.noalias.scope.decl(metadata !108)
  call void @llvm.experimental.noalias.scope.decl(metadata !111)
  call void @llvm.experimental.noalias.scope.decl(metadata !114)
  call void @llvm.experimental.noalias.scope.decl(metadata !117)
  call void @llvm.experimental.noalias.scope.decl(metadata !120)
  %_9.i.i.i.i.i = load i64, ptr %_5, align 8, !range !123, !alias.scope !124, !noundef !4
  %9 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %9, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb6
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_11.sroa.4.0._5.sroa_idx, align 8, !alias.scope !124, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !124
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb6
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(40) %self, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef range(i8 0, 12) %0, i8 noundef range(i8 0, 4) %1, i8 %2) unnamed_addr #1 {
start:
  %3 = getelementptr inbounds nuw i8, ptr %self, i64 38
  %4 = load i8, ptr %3, align 2, !range !125, !noundef !4
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
  %8 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 8, i32 noundef %_10.sroa.0.0) #22
  br label %bb8

bb8:                                              ; preds = %bb13, %bb5, %bb17
  %stored.sroa.0.0 = phi i8 [ %9, %bb5 ], [ %11, %bb13 ], [ %8, %bb17 ]
  switch i8 %stored.sroa.0.0, label %bb12 [
    i8 7, label %bb10
    i8 3, label %bb24
    i8 4, label %bb23
  ]

bb5:                                              ; preds = %bb2
  %9 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 128, i64 noundef -1) #22
  br label %bb8

bb14:                                             ; preds = %bb2
  %_21 = zext nneg i8 %0 to i32
  %_20 = shl nuw nsw i32 %_21, 8
  %10 = or disjoint i32 %_20, 2
  br label %bb13

bb13:                                             ; preds = %bb14, %bb2
  %_11.sroa.0.0 = phi i32 [ %10, %bb14 ], [ 0, %bb2 ]
  %11 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef 144, i32 noundef %_11.sroa.0.0) #22
  br label %bb8

bb10:                                             ; preds = %bb8
  %..i = select i1 %_7, i64 384, i64 256
  %.13.i = select i1 %_7, i64 260, i64 388
  %12 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %.13.i) #22, !noalias !126
  %.sroa.6.0.extract.shift.i = lshr i64 %12, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %13 = trunc i64 %12 to i1
  br i1 %13, label %bb12, label %bb17.i

bb17.i:                                           ; preds = %bb10
  %_9.i = and i32 %.sroa.6.0.extract.trunc.i, 1
  %14 = icmp ne i32 %_9.i, 0
  %15 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_11.i = load i32, ptr %15, align 8, !alias.scope !129, !noalias !126
  %_10.i = icmp ne i32 %_11.i, %.sroa.6.0.extract.trunc.i
  %or.cond.i = select i1 %14, i1 %_10.i, i1 false
  br i1 %or.cond.i, label %bb7.i, label %bb12

bb7.i:                                            ; preds = %bb17.i
  store i32 %.sroa.6.0.extract.trunc.i, ptr %15, align 8, !alias.scope !129, !noalias !126
  %16 = getelementptr inbounds nuw i8, ptr %self, i64 24
  %_13.i = load i32, ptr %16, align 8, !alias.scope !129, !noalias !126, !noundef !4
  %_12.i = add i32 %_13.i, 1
  store i32 %_12.i, ptr %16, align 8, !alias.scope !129, !noalias !126
  %17 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %..i, i32 noundef %_12.i) #22, !noalias !126
  %.not.i = icmp eq i8 %17, 7
  br i1 %.not.i, label %bb19.i, label %bb12

bb19.i:                                           ; preds = %bb7.i
  %18 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %self, i64 noundef %..i, i32 noundef 1) #22
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
define noundef range(i32 0, 1024) i32 @unir_consumer_ended(ptr noundef readonly captures(none) %c) unnamed_addr #20 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %1 = load i8, ptr %0, align 4, !range !101, !noundef !4
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
  tail call void @unir_host_free(ptr noundef nonnull %c, i64 noundef 112, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !131
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
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_9.i), !noalias !136
  %or.cond.i = icmp ugt i64 %0, 4294967295
  br i1 %or.cond.i, label %bb11.i, label %bb12.i

bb11.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !136
  br label %bb2

bb12.i:                                           ; preds = %bb9.i
  %6 = icmp ne ptr %vat, null
  tail call void @llvm.assume(i1 %6)
  %_26.i = trunc nuw i64 %0 to i32
  call void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr noalias noundef nonnull sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_9.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i32 noundef %_26.i) #22, !noalias !136
  %7 = load i8, ptr %_9.i, align 8, !range !125, !noalias !136, !noundef !4
  %8 = trunc nuw i8 %7 to i1
  br i1 %8, label %bb13.i, label %bb14.i

bb13.i:                                           ; preds = %bb12.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !136
  br label %bb2

bb14.i:                                           ; preds = %bb12.i
  %9 = getelementptr inbounds nuw i8, ptr %_9.i, i64 8
  %_30.0.i = load i64, ptr %9, align 8, !noalias !136, !noundef !4
  %10 = getelementptr inbounds nuw i8, ptr %_9.i, i64 16
  %_30.1.i = load i64, ptr %10, align 8, !noalias !136, !noundef !4
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !136
  call void @llvm.lifetime.start.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  %_20.i.i = zext i32 %1 to i64
  %_22.i.i = zext i32 %2 to i64
  %_19.i.i = mul nuw i64 %_22.i.i, %_20.i.i
  %_7.i.i = add nuw i64 %_19.i.i, 512
  %_4.i.i = icmp ult i64 %_30.1.i, %_7.i.i
  br i1 %_4.i.i, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, label %bb3.i.i

bb3.i.i:                                          ; preds = %bb14.i
  call void @llvm.lifetime.start.p0(i64 112, ptr nonnull %c.i.i), !noalias !140
  %11 = lshr i64 %_20.i.i, 2
  %spec.store.select.i.i = tail call i64 @llvm.umax.i64(i64 %11, i64 1)
  %12 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 16
  store i64 %_30.0.i, ptr %12, align 8, !noalias !140
  %_10.sroa.4.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 24
  store i64 %_30.1.i, ptr %_10.sroa.4.0..sroa_idx.i.i, align 8, !noalias !140
  %_10.sroa.5.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 32
  store i32 %1, ptr %_10.sroa.5.0..sroa_idx.i.i, align 8, !noalias !140
  %_10.sroa.6.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 36
  store i32 %2, ptr %_10.sroa.6.0..sroa_idx.i.i, align 4, !noalias !140
  %_10.sroa.7.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 40
  store i32 0, ptr %_10.sroa.7.0..sroa_idx.i.i, align 8, !noalias !140
  %_10.sroa.8.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 44
  store i32 0, ptr %_10.sroa.8.0..sroa_idx.i.i, align 4, !noalias !140
  %_10.sroa.9.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 48
  store i32 0, ptr %_10.sroa.9.0..sroa_idx.i.i, align 8, !noalias !140
  %_10.sroa.10.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 52
  store i8 4, ptr %_10.sroa.10.0..sroa_idx.i.i, align 4, !noalias !140
  %_10.sroa.12.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 54
  store i8 1, ptr %_10.sroa.12.0..sroa_idx.i.i, align 2, !noalias !140
  %13 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 56
  %14 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 72
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %13, i8 0, i64 16, i1 false), !noalias !140
  store i64 %spec.store.select.i.i, ptr %14, align 8, !noalias !140
  %15 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 80
  store i64 512, ptr %15, align 8, !noalias !140
  %16 = getelementptr inbounds nuw i8, ptr %c.i.i, i64 88
  store i64 0, ptr %c.i.i, align 8, !noalias !140
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(17) %16, i8 0, i64 17, i1 false), !noalias !140
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_15.i.i), !noalias !140
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_15.i.i, ptr noalias noundef align 8 dereferenceable(112) %c.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat) #22, !noalias !144
  %17 = load i8, ptr %_15.i.i, align 8, !range !145, !noalias !140, !noundef !4
  %.not.i.i = icmp eq i8 %17, 5
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_15.i.i), !noalias !140
  br i1 %.not.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb11.i.i

bb11.i.i:                                         ; preds = %bb3.i.i
  call void @llvm.lifetime.end.p0(i64 112, ptr nonnull %c.i.i), !noalias !140
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb3.i.i
  %_16.sroa.0.0.copyload1.i = load i64, ptr %c.i.i, align 8, !noalias !146
  %_16.sroa.7.0.c.i.sroa_idx.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 8
  %_16.sroa.7.0.copyload2.i = load i8, ptr %_16.sroa.7.0.c.i.sroa_idx.i, align 8, !noalias !146
  %_16.sroa.9.0.c.i.sroa_idx.i = getelementptr inbounds nuw i8, ptr %c.i.i, i64 9
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.i, ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.0.c.i.sroa_idx.i, i64 103, i1 false), !noalias !146
  call void @llvm.lifetime.end.p0(i64 112, ptr nonnull %c.i.i), !noalias !140
  %18 = icmp eq i64 %_16.sroa.0.0.copyload1.i, 2
  br i1 %18, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, label %bb5

_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6: ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb11.i.i, %bb14.i
  call void @llvm.lifetime.end.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  br label %bb2

bb5:                                              ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  call void @llvm.lifetime.start.p0(i64 103, ptr nonnull %_14.sroa.5)
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 1 dereferenceable(103) %_14.sroa.5, ptr noundef nonnull align 1 dereferenceable(103) %_16.sroa.9.i, i64 103, i1 false)
  call void @llvm.lifetime.end.p0(i64 103, ptr nonnull %_16.sroa.9.i)
  call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22
  %_0.i.i.i = call noalias noundef ptr @unir_host_alloc(i64 noundef 112, i64 noundef range(i64 1, -9223372036854775807) 8) #22
  %19 = icmp eq ptr %_0.i.i.i, null
  br i1 %19, label %bb6, label %bb7, !prof !147

bb2:                                              ; preds = %bb7, %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6, %bb13.i, %bb11.i, %start
  %_0.sroa.0.0 = phi ptr [ %_0.i.i.i, %bb7 ], [ null, %_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_.exit.thread6 ], [ null, %bb11.i ], [ null, %bb13.i ], [ null, %start ]
  ret ptr %_0.sroa.0.0

bb6:                                              ; preds = %bb5
  call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 112) #28
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
  %_4 = load i64, ptr %0, align 8, !noundef !4
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_6 = load i32, ptr %2, align 8, !noundef !4
  %_5 = zext i32 %_6 to i64
  %_3 = add i64 %_4, %_5
  %3 = load i64, ptr %self, align 8, !range !148, !noundef !4
  %4 = trunc nuw i64 %3 to i1
  %5 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %6 = load i64, ptr %5, align 8
  %7 = tail call i64 @llvm.umin.i64(i64 %6, i64 %_3)
  %spec.store.select = select i1 %4, i64 %7, i64 %_3
  %8 = getelementptr inbounds nuw i8, ptr %self, i64 88
  %_11 = load i64, ptr %8, align 8, !noundef !4
  %_9.not = icmp ugt i64 %spec.store.select, %_11
  br i1 %_9.not, label %bb2, label %bb1

bb2:                                              ; preds = %start
  store i64 %spec.store.select, ptr %8, align 8
  %9 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 136, i64 noundef %spec.store.select) #22
  %.not = icmp eq i8 %9, 7
  %10 = getelementptr inbounds nuw i8, ptr %self, i64 54
  %11 = load i8, ptr %10, align 2, !range !125
  br i1 %.not, label %bb17, label %bb5

bb1:                                              ; preds = %start
  store i8 5, ptr %_0, align 8
  br label %bb8

bb17:                                             ; preds = %bb2
  %12 = trunc nuw i8 %11 to i1
  %..i = select i1 %12, i64 384, i64 256
  %.13.i = select i1 %12, i64 260, i64 388
  %13 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %.13.i) #22, !noalias !149
  %.sroa.6.0.extract.shift.i = lshr i64 %13, 32
  %.sroa.6.0.extract.trunc.i = trunc nuw i64 %.sroa.6.0.extract.shift.i to i32
  %14 = trunc i64 %13 to i1
  br i1 %14, label %bb15, label %bb17.i

bb17.i:                                           ; preds = %bb17
  %_9.i = and i32 %.sroa.6.0.extract.trunc.i, 1
  %15 = icmp ne i32 %_9.i, 0
  %16 = getelementptr inbounds nuw i8, ptr %self, i64 48
  %_11.i = load i32, ptr %16, align 8, !alias.scope !152, !noalias !149
  %_10.i = icmp ne i32 %_11.i, %.sroa.6.0.extract.trunc.i
  %or.cond.i = select i1 %15, i1 %_10.i, i1 false
  br i1 %or.cond.i, label %bb7.i, label %bb6

bb7.i:                                            ; preds = %bb17.i
  store i32 %.sroa.6.0.extract.trunc.i, ptr %16, align 8, !alias.scope !152, !noalias !149
  %17 = getelementptr inbounds nuw i8, ptr %self, i64 40
  %_13.i = load i32, ptr %17, align 8, !alias.scope !152, !noalias !149, !noundef !4
  %_12.i = add i32 %_13.i, 1
  store i32 %_12.i, ptr %17, align 8, !alias.scope !152, !noalias !149
  %18 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %..i, i32 noundef %_12.i) #22, !noalias !149
  %.not.i = icmp eq i8 %18, 7
  br i1 %.not.i, label %bb19.i, label %bb5

bb19.i:                                           ; preds = %bb7.i
  %19 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %1, i64 noundef %..i, i32 noundef 1) #22
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
  tail call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef align 8 dereferenceable(88) %s, i8 noundef %switch.select10, i8 noundef %11, i8 %switch.select10) #22
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
  tail call void @llvm.experimental.noalias.scope.decl(metadata !154)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !157)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %attempt.i)
  store ptr %.buf, ptr %attempt.i, align 8, !noalias !159
  %4 = getelementptr inbounds nuw i8, ptr %attempt.i, i64 8
  store i64 %cap, ptr %4, align 8, !noalias !159
  %_45.sroa.5.0._7.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  %_45.sroa.6.0._7.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 16
  %_0.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 16
  %5 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %6 = getelementptr inbounds nuw i8, ptr %c, i64 44
  %_60.sroa.5.0._24.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_24.i, i64 8
  %_60.sroa.6.0._24.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_24.i, i64 16
  br label %bb26.i

bb27.i:                                           ; preds = %bb5.i
  %7 = load i8, ptr %5, align 2, !range !125, !alias.scope !162, !noalias !165, !noundef !4
  %_8.i.i = load i32, ptr %6, align 4, !alias.scope !162, !noalias !165, !noundef !4
  %_7.i.i = and i32 %_8.i.i, 1
  %_6.i.not.i = icmp eq i32 %_7.i.i, 0
  br i1 %_6.i.not.i, label %bb5.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

bb5.i.i:                                          ; preds = %bb27.i
  %_9.i.i = or disjoint i32 %_8.i.i, 1
  store i32 %_9.i.i, ptr %6, align 4, !alias.scope !162, !noalias !165
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb5.i.i, %bb27.i
  %_12.i.i = phi i32 [ %_8.i.i, %bb27.i ], [ %_9.i.i, %bb5.i.i ]
  %8 = trunc nuw i8 %7 to i1
  %..i.i = select i1 %8, i64 388, i64 260
  %_0.i.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i, i32 noundef %_12.i.i) #22, !noalias !154
  %.not.i = icmp eq i8 %_0.i.i, 7
  br i1 %.not.i, label %bb32.i, label %bb9.i

bb26.i:                                           ; preds = %bb26.i.backedge, %start
  %iter.sroa.0.051.i = phi i64 [ 0, %start ], [ %iter.sroa.0.051.i.be, %bb26.i.backedge ]
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_7.i), !noalias !159
  call fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([24 x i8]) align 8 captures(address) dereferenceable(24) %_7.i, ptr noalias noundef align 8 dereferenceable(16) %attempt.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #30, !noalias !154
  %9 = load i64, ptr %_7.i, align 8, !range !166, !noalias !159, !noundef !4
  %10 = icmp eq i64 %9, 2
  %_46.sroa.0.0.copyload.i = load i64, ptr %_45.sroa.5.0._7.sroa_idx.i, align 8, !noalias !159
  %_46.sroa.5.0.copyload.i = load i16, ptr %_45.sroa.6.0._7.sroa_idx.i, align 8, !noalias !159
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !159
  %11 = trunc nuw i64 %9 to i1
  %or.cond = select i1 %10, i1 true, i1 %11
  br i1 %or.cond, label %bb25.i, label %bb5.i

bb32.i:                                           ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %..i = select i1 %8, i64 256, i64 384
  %12 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %_0.i.i.i, i64 noundef %..i) #22, !noalias !154
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
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %14, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select27.i, i8 noundef %7, i8 %switch.select27.i) #22
  br label %bb2.sink.split

bb10.i:                                           ; preds = %bb32.i
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_24.i), !noalias !159
  call fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([24 x i8]) align 8 captures(address) dereferenceable(24) %_24.i, ptr noalias noundef align 8 dereferenceable(16) %attempt.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #30, !noalias !154
  %15 = load i64, ptr %_24.i, align 8, !range !166, !noalias !159, !noundef !4
  %16 = icmp eq i64 %15, 2
  %_61.sroa.0.0.copyload.i = load i64, ptr %_60.sroa.5.0._24.sroa_idx.i, align 8, !noalias !159
  %_61.sroa.5.0.copyload.i = load i16, ptr %_60.sroa.6.0._24.sroa_idx.i, align 8, !noalias !159
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_24.i), !noalias !159
  br i1 %16, label %bb42.i, label %bb43.i

bb42.i:                                           ; preds = %bb10.i
  %17 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_61.sroa.0.0.copyload.i, ptr %17, align 8, !alias.scope !154, !noalias !167
  %_63.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_61.sroa.5.0.copyload.i, ptr %_63.sroa.2.0..sroa_idx.i, align 8, !alias.scope !154, !noalias !167
  br label %bb2.sink.split

bb43.i:                                           ; preds = %bb10.i
  %18 = trunc nuw i64 %15 to i1
  br i1 %18, label %bb13.i, label %bb12.i

bb13.i:                                           ; preds = %bb43.i
  %19 = call fastcc noundef i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, i1 noundef zeroext false) #22, !noalias !154
  %.not25.i = icmp eq i8 %19, 7
  br i1 %.not25.i, label %bb17.i, label %bb16.i

bb12.i:                                           ; preds = %bb43.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_38.i), !noalias !159
  call void @llvm.experimental.noalias.scope.decl(metadata !168)
  call void @llvm.experimental.noalias.scope.decl(metadata !171)
  %20 = load i8, ptr %5, align 2, !range !125, !alias.scope !173, !noalias !174, !noundef !4
  %21 = trunc nuw i8 %20 to i1
  %..i34.i = select i1 %21, i64 256, i64 384
  %22 = call { i1, i8 } @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate4wait(ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i34.i, i32 noundef %.sroa.6.0.extract.trunc.i, i64 noundef range(i64 0, 2) %_11.sroa.0.0, i64 %timeout_ns) #22, !noalias !176
  %23 = extractvalue { i1, i8 } %22, 0
  %24 = extractvalue { i1, i8 } %22, 1
  br i1 %23, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb13.i.i

bb13.i.i:                                         ; preds = %bb12.i
  %_8.i.i.i = load i32, ptr %6, align 4, !alias.scope !177, !noalias !174, !noundef !4
  %_7.i.i.i = and i32 %_8.i.i.i, 1
  %_6.i.not.i.i = icmp eq i32 %_7.i.i.i, 0
  br i1 %_6.i.not.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, label %bb5.i.i.i

bb5.i.i.i:                                        ; preds = %bb13.i.i
  %_9.i.i.i = add i32 %_8.i.i.i, 1
  store i32 %_9.i.i.i, ptr %6, align 4, !alias.scope !177, !noalias !174
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i.i.i, %bb13.i.i
  %_12.i.i.i = phi i32 [ %_8.i.i.i, %bb13.i.i ], [ %_9.i.i.i, %bb5.i.i.i ]
  %..i.i.i = select i1 %21, i64 388, i64 260
  %_0.i.i35.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i.i, i32 noundef %_12.i.i.i) #22, !noalias !176
  %.not.not.i.i = icmp eq i8 %_0.i.i35.i, 7
  br i1 %.not.not.i.i, label %bb6.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %switch.i.i = icmp ult i8 %24, 2
  br i1 %switch.i.i, label %bb50.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i: ; preds = %bb6.i.i
  store i8 1, ptr %_38.i, align 8, !alias.scope !168, !noalias !180
  br label %bb49.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb12.i
  %waited.sroa.4.0.i.i = phi i8 [ %_0.i.i35.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i ], [ %24, %bb12.i ]
  %switch.selectcmp.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_38.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select8.i.i, i8 noundef %20, i8 %switch.select8.i.i) #22, !noalias !154
  %.pr.i = load i8, ptr %_38.i, align 8, !noalias !159
  %.not24.i = icmp eq i8 %.pr.i, 5
  br i1 %.not24.i, label %bb50.i, label %bb49.i

bb16.i:                                           ; preds = %bb13.i
  %switch.selectcmp28.i = icmp eq i8 %19, 4
  %switch.select29.i = select i1 %switch.selectcmp28.i, i8 8, i8 2
  %switch.selectcmp30.i = icmp eq i8 %19, 3
  %switch.select31.i = select i1 %switch.selectcmp30.i, i8 6, i8 %switch.select29.i
  %25 = load i8, ptr %5, align 2, !range !125, !alias.scope !157, !noalias !165, !noundef !4
  %26 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %26, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select31.i, i8 noundef %25, i8 %switch.select31.i) #22
  br label %bb2.sink.split

bb17.i:                                           ; preds = %bb13.i
  %27 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_61.sroa.0.0.copyload.i, ptr %27, align 8, !alias.scope !154, !noalias !167
  %28 = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_61.sroa.5.0.copyload.i, ptr %28, align 8, !alias.scope !154, !noalias !167
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %attempt.i)
  br label %bb3

bb49.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread37.i
  %29 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %29, ptr noundef nonnull align 8 dereferenceable(16) %_38.i, i64 16, i1 false), !noalias !167
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !159
  br label %bb2.sink.split

bb50.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb6.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !159
  br label %bb26.i.backedge

bb5.i:                                            ; preds = %bb26.i
  %_42.i = add nuw nsw i64 %iter.sroa.0.051.i, 1
  call void @llvm.x86.sse2.pause(), !noalias !154
  %exitcond.not.i = icmp eq i64 %_42.i, 64
  br i1 %exitcond.not.i, label %bb27.i, label %bb26.i.backedge

bb26.i.backedge:                                  ; preds = %bb5.i, %bb50.i
  %iter.sroa.0.051.i.be = phi i64 [ %_42.i, %bb5.i ], [ 0, %bb50.i ]
  br label %bb26.i

bb25.i:                                           ; preds = %bb26.i
  %30 = getelementptr inbounds nuw i8, ptr %_8, i64 8
  store i64 %_46.sroa.0.0.copyload.i, ptr %30, align 8, !alias.scope !154, !noalias !167
  %_48.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_8, i64 16
  store i16 %_46.sroa.5.0.copyload.i, ptr %_48.sroa.2.0..sroa_idx.i, align 8, !alias.scope !154, !noalias !167
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
  %f = load i64, ptr %32, align 8, !noundef !4
  %33 = icmp eq ptr %substream, null
  br i1 %33, label %bb7, label %bb5

bb5:                                              ; preds = %bb3
  %34 = getelementptr inbounds nuw i8, ptr %_8, i64 16
  %f2 = load i16, ptr %34, align 8, !noundef !4
  store i16 %f2, ptr %substream, align 2
  br label %bb7

bb7:                                              ; preds = %bb5, %bb3, %bb2
  %_0.sroa.0.0 = phi i64 [ %f, %bb3 ], [ %f, %bb5 ], [ %switch.offset, %bb2 ]
  call void @llvm.experimental.noalias.scope.decl(metadata !181)
  call void @llvm.experimental.noalias.scope.decl(metadata !184)
  call void @llvm.experimental.noalias.scope.decl(metadata !187)
  call void @llvm.experimental.noalias.scope.decl(metadata !190)
  call void @llvm.experimental.noalias.scope.decl(metadata !193)
  %_9.i.i.i.i.i = load i64, ptr %_10, align 8, !range !123, !alias.scope !196, !noundef !4
  %35 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %35, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb7
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_21.sroa.4.0._10.sroa_idx, align 8, !alias.scope !196, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !196
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb7
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_10)
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_8)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: inlinehint nounwind nonlazybind uwtable
define internal fastcc void @_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4readNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_0, ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(16) %_1, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #21 {
start:
  %_20.i.i = alloca [16 x i8], align 8
  %_8.i.i.i.i = alloca [16 x i8], align 8
  %h.i.i = alloca [8 x i8], align 8
  %_19.i.i = alloca [16 x i8], align 8
  %_10.i.i = alloca [16 x i8], align 8
  %_23.i = alloca [16 x i8], align 8
  %_7.i = alloca [24 x i8], align 8
  %_4.0 = load ptr, ptr %_1, align 8, !nonnull !4, !align !5, !noundef !4
  %0 = getelementptr inbounds nuw i8, ptr %_1, i64 8
  %_4.1 = load i64, ptr %0, align 8, !noundef !4
  tail call void @llvm.experimental.noalias.scope.decl(metadata !197)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !200)
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_7.i), !noalias !202
  tail call void @llvm.experimental.noalias.scope.decl(metadata !205)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !208)
  %1 = getelementptr inbounds nuw i8, ptr %c, i64 16
  %2 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %3 = load i8, ptr %2, align 4, !range !101, !alias.scope !210, !noalias !211, !noundef !4
  %.not10.i.i = icmp eq i8 %3, 4
  br i1 %.not10.i.i, label %bb26.i.i, label %bb27.i.i

bb27.i.i:                                         ; preds = %start
  %4 = getelementptr inbounds nuw i8, ptr %c, i64 53
  %_46.1.i.i = load i8, ptr %4, align 1, !alias.scope !210, !noalias !211
  %5 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  store i8 0, ptr %5, align 8, !alias.scope !205, !noalias !213
  %_50.sroa.2.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 9
  store i8 %3, ptr %_50.sroa.2.0..sroa_idx.i.i, align 1, !alias.scope !205, !noalias !213
  %_50.sroa.3.0..sroa_idx.i.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 10
  store i8 %_46.1.i.i, ptr %_50.sroa.3.0..sroa_idx.i.i, align 2, !alias.scope !205, !noalias !213
  br label %bb11.i

bb26.i.i:                                         ; preds = %start
  %6 = getelementptr inbounds nuw i8, ptr %c, i64 56
  %_7.i.i = load i64, ptr %6, align 8, !alias.scope !210, !noalias !211, !noundef !4
  %7 = getelementptr inbounds nuw i8, ptr %c, i64 96
  %_8.i.i = load i64, ptr %7, align 8, !alias.scope !210, !noalias !211, !noundef !4
  %_6.i.i = icmp eq i64 %_7.i.i, %_8.i.i
  br i1 %_6.i.i, label %bb2.i.i, label %bb11.i.i

bb2.i.i:                                          ; preds = %bb26.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_10.i.i), !noalias !214
  tail call void @llvm.experimental.noalias.scope.decl(metadata !215)
  %8 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 8) #22, !noalias !218
  %.sroa.6.0.extract.shift.i.i.i.i = lshr i64 %8, 32
  %9 = trunc i64 %8 to i1
  br i1 %9, label %bb6.i.i.i.i, label %bb7.i.i.i.i

bb6.i.i.i.i:                                      ; preds = %bb2.i.i
  %.sroa.4.0.extract.shift.i.i.i.i = lshr i64 %8, 8
  %.sroa.4.0.extract.trunc.i.i.i.i = trunc i64 %.sroa.4.0.extract.shift.i.i.i.i to i8
  br label %bb3.i.i.i

bb7.i.i.i.i:                                      ; preds = %bb2.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !224
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8.i.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 0) #22, !noalias !218
  %10 = load i8, ptr %_8.i.i.i.i, align 8, !range !125, !noalias !224, !noundef !4
  %11 = trunc nuw i8 %10 to i1
  br i1 %11, label %bb8.i.i.i.i, label %bb4.i.i.i

bb8.i.i.i.i:                                      ; preds = %bb7.i.i.i.i
  %12 = getelementptr inbounds nuw i8, ptr %_8.i.i.i.i, i64 1
  %_20.i.i.i.i = load i8, ptr %12, align 1, !range !225, !noalias !224, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !224
  br label %bb3.i.i.i

bb3.i.i.i:                                        ; preds = %bb8.i.i.i.i, %bb6.i.i.i.i
  %_4.sroa.5.0.ph.i.i.i = phi i8 [ %_20.i.i.i.i, %bb8.i.i.i.i ], [ %.sroa.4.0.extract.trunc.i.i.i.i, %bb6.i.i.i.i ]
  %switch.selectcmp.i.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i.i, 4
  %switch.select.i.i.i = select i1 %switch.selectcmp.i.i.i, i8 8, i8 2
  %switch.selectcmp8.i.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i.i, 3
  %switch.select9.i.i.i = select i1 %switch.selectcmp8.i.i.i, i8 6, i8 %switch.select.i.i.i
  %13 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %14 = load i8, ptr %13, align 2, !range !125, !alias.scope !226, !noalias !227, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select9.i.i.i, i8 noundef %14, i8 %switch.select9.i.i.i) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb4.i.i.i:                                        ; preds = %bb7.i.i.i.i
  %15 = getelementptr inbounds nuw i8, ptr %_8.i.i.i.i, i64 8
  %_19.i.i.i.i = load i64, ptr %15, align 8, !noalias !224, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i.i), !noalias !224
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
  %_14.i.i.i.i = load i8, ptr %19, align 1, !range !229, !noalias !230, !noundef !4
  br label %bb7.i.i.i

bb6.i.i.i:                                        ; preds = %bb4.i.i.i.i, %bb5.i.i.i.i, %bb6.i11.i.i.i, %bb2.i.i.i.i, %bb4.i.i.i
  %20 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %21 = load i8, ptr %20, align 2, !range !125, !alias.scope !226, !noalias !227, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %21, i8 3) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb7.i.i.i:                                        ; preds = %bb14.i.i.i.i, %bb5.i.i.i.i, %bb6.i11.i.i.i
  %_0.sroa.8.0.i.i.i.i = phi i8 [ %_14.i.i.i.i, %bb14.i.i.i.i ], [ 10, %bb6.i11.i.i.i ], [ 11, %bb5.i.i.i.i ]
  %_34.i.i.i = icmp ult i64 %_19.i.i.i.i, %_7.i.i
  br i1 %_34.i.i.i, label %bb33.i.i.i, label %bb21.i.i.i

bb21.i.i.i:                                       ; preds = %bb7.i.i.i
  %_20.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 88
  %_38.i.i.i = load i64, ptr %_20.i.i.i, align 8, !alias.scope !226, !noalias !227, !noundef !4
  %_39.i.i.i = icmp ugt i64 %_19.i.i.i.i, %_38.i.i.i
  br i1 %_39.i.i.i, label %bb33.i.i.i, label %bb24.i.i.i

bb24.i.i.i:                                       ; preds = %bb21.i.i.i
  %_44.i.i.i = icmp eq i64 %_7.i.i, -1
  br i1 %_44.i.i.i, label %bb34.i.i.i, label %bb26.i.i.i

bb26.i.i.i:                                       ; preds = %bb24.i.i.i
  %_22.i.i.i = getelementptr inbounds nuw i8, ptr %c, i64 32
  %_43.i.i.i = load i32, ptr %_22.i.i.i, align 8, !alias.scope !226, !noalias !227, !noundef !4
  %_42.i.i.i = zext i32 %_43.i.i.i to i64
  %_47.i.i.i = sub i64 %_19.i.i.i.i, %_7.i.i
  %_46.i.i.i = icmp ugt i64 %_47.i.i.i, %_42.i.i.i
  br i1 %_46.i.i.i, label %bb33.i.i.i, label %bb34.i.i.i

bb34.i.i.i:                                       ; preds = %bb26.i.i.i, %bb24.i.i.i
  store i64 %_19.i.i.i.i, ptr %7, align 8, !alias.scope !226, !noalias !227
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
  %25 = load i8, ptr %24, align 2, !range !125, !alias.scope !226, !noalias !227, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %checked.sroa.0.0.i.i.i, i8 noundef %25, i8 %checked.sroa.0.0.i.i.i) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb2.i.i.i:                                        ; preds = %bb34.i.i.i
  unreachable

bb9.i.i.i:                                        ; preds = %bb34.i.i.i
  %26 = getelementptr inbounds nuw i8, ptr %c, i64 104
  store i8 1, ptr %26, align 8, !alias.scope !226, !noalias !227
  br label %bb29.i.i

bb10.i.i.i:                                       ; preds = %bb34.i.i.i
  %27 = icmp samesign ult i8 %_0.sroa.8.0.i.i.i.i, 10
  tail call void @llvm.assume(i1 %27)
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_10.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %_0.sroa.8.0.i.i.i.i, i8 noundef 0, i8 %_0.sroa.8.0.i.i.i.i) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb10.i.i.i, %bb33.i.i.i, %bb6.i.i.i, %bb3.i.i.i
  %.pr.i.i = load i8, ptr %_10.i.i, align 8, !noalias !214
  %.not11.i.i = icmp eq i8 %.pr.i.i, 5
  br i1 %.not11.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i, label %bb28.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i: ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %_13.pre.i.i = load i64, ptr %6, align 8, !alias.scope !210, !noalias !211
  %_14.pre.i.i = load i64, ptr %7, align 8, !alias.scope !210, !noalias !211
  br label %bb29.i.i

bb11.i.i:                                         ; preds = %bb29.i.i, %bb26.i.i
  %28 = phi i64 [ %_13.i.i, %bb29.i.i ], [ %_7.i.i, %bb26.i.i ]
  %29 = getelementptr inbounds nuw i8, ptr %c, i64 80
  %off.i.i = load i64, ptr %29, align 8, !alias.scope !210, !noalias !211, !noundef !4
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %h.i.i), !noalias !214
  store i64 0, ptr %h.i.i, align 8, !noalias !214
  %30 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region4read(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef %off.i.i, ptr noalias noundef nonnull align 1 %h.i.i, i64 noundef 8) #22, !noalias !228
  %.not12.i.i = icmp eq i8 %30, 7
  br i1 %.not12.i.i, label %bb14.i.i, label %bb13.i.i

bb28.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %31 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %31, ptr noundef nonnull align 8 dereferenceable(16) %_10.i.i, i64 16, i1 false), !noalias !213
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_10.i.i), !noalias !214
  br label %bb11.i

bb29.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i, %bb9.i.i.i, %bb34.i.i.i
  %_14.i.i = phi i64 [ %_14.pre.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i ], [ %_19.i.i.i.i, %bb9.i.i.i ], [ %_19.i.i.i.i, %bb34.i.i.i ]
  %_13.i.i = phi i64 [ %_13.pre.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb29_crit_edge.i.i ], [ %_7.i.i, %bb9.i.i.i ], [ %_7.i.i, %bb34.i.i.i ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_10.i.i), !noalias !214
  %_12.i.i = icmp eq i64 %_13.i.i, %_14.i.i
  br i1 %_12.i.i, label %bb5.i.i, label %bb11.i.i

bb5.i.i:                                          ; preds = %bb29.i.i
  %32 = getelementptr inbounds nuw i8, ptr %c, i64 104
  %33 = load i8, ptr %32, align 8, !range !125, !alias.scope !210, !noalias !211, !noundef !4
  %_15.i.i = trunc nuw i8 %33 to i1
  br i1 %_15.i.i, label %bb7.i.i, label %bb9.i.i

bb13.i.i:                                         ; preds = %bb11.i.i
  %switch.selectcmp.i.i = icmp eq i8 %30, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp14.i.i = icmp eq i8 %30, 3
  %switch.select15.i.i = select i1 %switch.selectcmp14.i.i, i8 6, i8 %switch.select.i.i
  %34 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %35 = load i8, ptr %34, align 2, !range !125, !alias.scope !210, !noalias !211, !noundef !4
  %36 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %36, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select15.i.i, i8 noundef %35, i8 %switch.select15.i.i) #22, !noalias !231
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !214
  br label %bb11.i

bb14.i.i:                                         ; preds = %bb11.i.i
  %_31.sroa.0.0.copyload.i.i = load i64, ptr %h.i.i, align 8, !noalias !214
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
  %42 = load i8, ptr %41, align 2, !range !125, !alias.scope !210, !noalias !211, !noundef !4
  %43 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %43, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %42, i8 3) #22, !noalias !231
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !214
  br label %bb11.i

bb18.i.i:                                         ; preds = %bb5.i.i.i
  %_39.i.i = and i64 %_31.sroa.0.0.copyload.i.i, 4294967295
  %44 = getelementptr inbounds nuw i8, ptr %c, i64 36
  %_67.i.i = load i32, ptr %44, align 4, !alias.scope !210, !noalias !211, !noundef !4
  %_66.i.i = zext i32 %_67.i.i to i64
  %_41.i.i = add nsw i64 %_66.i.i, -8
  %_38.not.i.i = icmp ult i64 %_41.i.i, %_39.i.i
  br i1 %_38.not.i.i, label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i, label %bb4.i

bb9.i.i:                                          ; preds = %bb5.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_19.i.i), !noalias !214
  %45 = getelementptr inbounds nuw i8, ptr %c, i64 64
  %_5.i.i.i = load i64, ptr %45, align 8, !alias.scope !232, !noalias !235, !noundef !4
  %_3.i.i.i = icmp eq i64 %_14.i.i, %_5.i.i.i
  br i1 %_3.i.i.i, label %bb3.i, label %bb2.i22.i.i

bb2.i22.i.i:                                      ; preds = %bb9.i.i
  %46 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 128, i64 noundef %_14.i.i) #22, !noalias !238
  %.not.i.i.i = icmp eq i8 %46, 7
  br i1 %.not.i.i.i, label %bb5.i27.i.i, label %bb4.i23.i.i

bb4.i23.i.i:                                      ; preds = %bb2.i22.i.i
  %switch.selectcmp.i24.i.i = icmp eq i8 %46, 4
  %switch.select.i25.i.i = select i1 %switch.selectcmp.i24.i.i, i8 8, i8 2
  %switch.selectcmp3.i.i.i = icmp eq i8 %46, 3
  %switch.select4.i.i.i = select i1 %switch.selectcmp3.i.i.i, i8 6, i8 %switch.select.i25.i.i
  %47 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %48 = load i8, ptr %47, align 2, !range !125, !alias.scope !232, !noalias !235, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_19.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select4.i.i.i, i8 noundef %48, i8 %switch.select4.i.i.i) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

bb5.i27.i.i:                                      ; preds = %bb2.i22.i.i
  store i64 %_14.i.i, ptr %45, align 8, !alias.scope !232, !noalias !235
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_19.i.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #22, !noalias !228
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i27.i.i, %bb4.i23.i.i
  %.pr36.i.i = load i8, ptr %_19.i.i, align 8, !noalias !214
  %.not.i.i = icmp eq i8 %.pr36.i.i, 5
  br i1 %.not.i.i, label %bb3.i, label %bb30.i.i

bb7.i.i:                                          ; preds = %bb5.i.i
  %49 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %49, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 11, i8 noundef 2, i8 undef) #22, !noalias !231
  br label %bb11.i

bb30.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %50 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %50, ptr noundef nonnull align 8 dereferenceable(16) %_19.i.i, i64 16, i1 false), !noalias !213
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_19.i.i), !noalias !214
  br label %bb11.i

bb11.i:                                           ; preds = %bb30.i.i, %bb7.i.i, %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread40.i, %bb13.i.i, %bb28.i.i, %bb27.i.i
  %51 = getelementptr inbounds nuw i8, ptr %_7.i, i64 8
  %_29.sroa.0.0.copyload.i = load i64, ptr %51, align 8, !noalias !202
  %_29.sroa.5.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 16
  %_29.sroa.5.0.copyload.i = load i32, ptr %_29.sroa.5.0..sroa_idx.i, align 8, !noalias !202
  %_29.sroa.6.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 20
  %_29.sroa.6.0.copyload.i = load i16, ptr %_29.sroa.6.0..sroa_idx.i, align 4, !noalias !202
  %_29.sroa.7.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_7.i, i64 22
  %_29.sroa.7.0.copyload.i = load i16, ptr %_29.sroa.7.0..sroa_idx.i, align 2, !noalias !202
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !202
  %52 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %_29.sroa.0.0.copyload.i, ptr %52, align 8, !alias.scope !197, !noalias !239
  %_31.sroa.2.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i32 %_29.sroa.5.0.copyload.i, ptr %_31.sroa.2.0..sroa_idx.i, align 8, !alias.scope !197, !noalias !239
  %_31.sroa.3.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 20
  store i16 %_29.sroa.6.0.copyload.i, ptr %_31.sroa.3.0..sroa_idx.i, align 4, !alias.scope !197, !noalias !239
  %_31.sroa.4.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 22
  store i16 %_29.sroa.7.0.copyload.i, ptr %_31.sroa.4.0..sroa_idx.i, align 2, !alias.scope !197, !noalias !239
  store i64 2, ptr %_0, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb18.i.i
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %h.i.i), !noalias !214
  %53 = lshr i64 %_31.sroa.0.0.copyload.i.i, 32
  %54 = trunc nuw i64 %53 to i16
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !202
  %_32.not.i = icmp samesign ult i64 %_4.1, %_39.i.i
  br i1 %_32.not.i, label %bb15.i, label %bb13.i

bb3.i:                                            ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb9.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_19.i.i), !noalias !214
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_7.i), !noalias !202
  store i64 0, ptr %_0, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb13.i:                                           ; preds = %bb4.i
  %_17.i = add i64 %off.i.i, 8
  %55 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region4read(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef %_17.i, ptr noalias noundef nonnull align 1 %_4.0, i64 noundef %_39.i.i) #22, !noalias !197
  %.not.i = icmp eq i8 %55, 7
  br i1 %.not.i, label %bb7.i, label %bb6.i

bb15.i:                                           ; preds = %bb4.i
  %56 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i8 3, ptr %56, align 8, !alias.scope !197, !noalias !239
  %_12.sroa.413.0..sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i64 %_39.i.i, ptr %_12.sroa.413.0..sroa_idx.i, align 8, !alias.scope !197, !noalias !239
  store i64 2, ptr %_0, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb6.i:                                            ; preds = %bb13.i
  %switch.selectcmp.i = icmp eq i8 %55, 4
  %switch.select.i = select i1 %switch.selectcmp.i, i8 8, i8 2
  %switch.selectcmp17.i = icmp eq i8 %55, 3
  %switch.select18.i = select i1 %switch.selectcmp17.i, i8 6, i8 %switch.select.i
  %57 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %58 = load i8, ptr %57, align 2, !range !125, !alias.scope !200, !noalias !240, !noundef !4
  %59 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %59, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select18.i, i8 noundef %58, i8 %switch.select18.i) #22
  store i64 2, ptr %_0, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb7.i:                                            ; preds = %bb13.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_23.i), !noalias !202
  call void @llvm.experimental.noalias.scope.decl(metadata !241)
  %60 = add i64 %28, 1
  store i64 %60, ptr %6, align 8, !alias.scope !244, !noalias !245
  %61 = getelementptr inbounds nuw i8, ptr %c, i64 32
  %62 = add i64 %off.i.i, %_66.i.i
  %_28.i.i = load i32, ptr %61, align 8, !alias.scope !244, !noalias !245, !noundef !4
  %_27.i.i = zext i32 %_28.i.i to i64
  %_26.i.i = mul nuw i64 %_27.i.i, %_66.i.i
  %_25.i.i = add nuw i64 %_26.i.i, 512
  %_24.i.i = icmp eq i64 %62, %_25.i.i
  %spec.store.select.i.i = select i1 %_24.i.i, i64 512, i64 %62
  store i64 %spec.store.select.i.i, ptr %29, align 8, !alias.scope !244, !noalias !245
  %63 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 260) #22, !noalias !248
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
  %66 = load i8, ptr %65, align 2, !range !125, !alias.scope !244, !noalias !245, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_23.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select6.i.i, i8 noundef %66, i8 %switch.select6.i.i) #22, !noalias !197
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb7.i
  %67 = and i64 %63, 4294967296
  %producer_parked.i.i = icmp ne i64 %67, 0
  %68 = getelementptr inbounds nuw i8, ptr %c, i64 64
  %_17.i.i = load i64, ptr %68, align 8, !alias.scope !244, !noalias !245, !noundef !4
  %_15.i19.i = sub i64 %60, %_17.i.i
  %69 = getelementptr inbounds nuw i8, ptr %c, i64 72
  %_18.i.i = load i64, ptr %69, align 8, !alias.scope !244, !noalias !245, !noundef !4
  %_14.i20.i = icmp uge i64 %_15.i19.i, %_18.i.i
  %brmerge.i.i = or i1 %producer_parked.i.i, %_14.i20.i
  br i1 %brmerge.i.i, label %bb7.i22.i, label %bb24.i

bb7.i22.i:                                        ; preds = %bb4.i.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_20.i.i), !noalias !249
  %_3.i.i23.i = icmp eq i64 %60, %_17.i.i
  br i1 %_3.i.i23.i, label %bb20.i.i, label %bb2.i.i24.i

bb2.i.i24.i:                                      ; preds = %bb7.i22.i
  %70 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %1, i64 noundef 128, i64 noundef %60) #22, !noalias !250
  %.not.i.i25.i = icmp eq i8 %70, 7
  br i1 %.not.i.i25.i, label %bb5.i.i35.i, label %bb4.i.i26.i

bb4.i.i26.i:                                      ; preds = %bb2.i.i24.i
  %switch.selectcmp.i.i27.i = icmp eq i8 %70, 4
  %switch.select.i.i28.i = select i1 %switch.selectcmp.i.i27.i, i8 8, i8 2
  %switch.selectcmp3.i.i29.i = icmp eq i8 %70, 3
  %switch.select4.i.i30.i = select i1 %switch.selectcmp3.i.i29.i, i8 6, i8 %switch.select.i.i28.i
  %71 = getelementptr inbounds nuw i8, ptr %c, i64 54
  %72 = load i8, ptr %71, align 2, !range !125, !alias.scope !254, !noalias !256, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_20.i.i, ptr noalias noundef align 8 dereferenceable(40) %1, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select4.i.i30.i, i8 noundef %72, i8 %switch.select4.i.i30.i) #22, !noalias !257
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i

bb5.i.i35.i:                                      ; preds = %bb2.i.i24.i
  store i64 %60, ptr %68, align 8, !alias.scope !254, !noalias !256
  call fastcc void @_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5grantNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_20.i.i, ptr noalias noundef nonnull align 8 dereferenceable(112) %c, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #22, !noalias !257
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i: ; preds = %bb5.i.i35.i, %bb4.i.i26.i
  %.pr.i32.i = load i8, ptr %_20.i.i, align 8, !noalias !249
  %.not.i33.i = icmp eq i8 %.pr.i32.i, 5
  br i1 %.not.i33.i, label %bb20.i.i, label %bb19.i34.i

bb19.i34.i:                                       ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_23.i, ptr noundef nonnull align 8 dereferenceable(16) %_20.i.i, i64 16, i1 false), !noalias !258
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_20.i.i), !noalias !249
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb20.i.i:                                         ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i31.i, %bb7.i22.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_20.i.i), !noalias !249
  br label %bb24.i

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb19.i34.i, %bb3.i.i
  %.pr.i = load i8, ptr %_23.i, align 8, !noalias !202
  %.not16.i = icmp eq i8 %.pr.i, 5
  br i1 %.not16.i, label %bb24.i, label %bb23.i

bb23.i:                                           ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  %73 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %73, ptr noundef nonnull align 8 dereferenceable(16) %_23.i, i64 16, i1 false), !noalias !239
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_23.i), !noalias !202
  store i64 2, ptr %_0, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb24.i:                                           ; preds = %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb20.i.i, %bb4.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_23.i), !noalias !202
  store i64 1, ptr %_0, align 8, !alias.scope !197, !noalias !239
  %_25.sroa.4.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %_39.i.i, ptr %_25.sroa.4.0._0.sroa_idx.i, align 8, !alias.scope !197, !noalias !239
  %_25.sroa.5.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i16 %54, ptr %_25.sroa.5.0._0.sroa_idx.i, align 8, !alias.scope !197, !noalias !239
  br label %_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb24.i, %bb23.i, %bb6.i, %bb15.i, %bb3.i, %bb11.i
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc noundef range(i8 0, 8) i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(40) %self, i1 noundef zeroext %up) unnamed_addr #1 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 38
  %1 = load i8, ptr %0, align 2, !range !125, !noundef !4
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 28
  %_8 = load i32, ptr %2, align 4, !noundef !4
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
  %_0 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef %_12) #22
  ret i8 %_0
}

; Function Attrs: nounwind
declare void @llvm.x86.sse2.pause() unnamed_addr #22

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
  %_11 = load i8, ptr %0, align 1, !range !229, !noundef !4
  tail call void @llvm.experimental.noalias.scope.decl(metadata !259)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5.i), !noalias !259
  %1 = getelementptr inbounds nuw i8, ptr %_5.i, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_5.i, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false), !noalias !259
  store i64 65536, ptr %2, align 8, !noalias !259
  %3 = getelementptr inbounds nuw i8, ptr %_5.i, i64 64
  store i64 0, ptr %_5.i, align 8, !noalias !259
  %_12.sroa.4.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false), !noalias !259
  store ptr inttoptr (i64 8 to ptr), ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !noalias !259
  %_12.sroa.5.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 16
  store i64 0, ptr %_12.sroa.5.0._5.sroa_idx.i, align 8, !noalias !259
  tail call void @llvm.experimental.noalias.scope.decl(metadata !262)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i.i), !noalias !259
  %4 = getelementptr inbounds nuw i8, ptr %c, i64 52
  %5 = load i8, ptr %4, align 4, !range !101, !alias.scope !265, !noalias !266, !noundef !4
  %.not.i.i = icmp eq i8 %5, 4
  br i1 %.not.i.i, label %bb8.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i: ; preds = %bb3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !259
  br label %bb6.i

bb8.i.i:                                          ; preds = %bb3
  %_14.i = getelementptr inbounds nuw i8, ptr %c, i64 16
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_14.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5.i, i8 noundef range(i8 0, 10) %_11, i8 noundef 1, i8 range(i8 0, 10) %_11) #22, !noalias !269
  %6 = load i8, ptr %err.i.i, align 8, !range !101, !noalias !270, !noundef !4
  %7 = icmp eq i8 %6, 0
  br i1 %7, label %bb4.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb8.i.i
  %8 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 1
  %9 = load i8, ptr %8, align 1, !range !107, !noalias !270, !noundef !4
  %10 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 2
  %_23.i.i = load i8, ptr %10, align 2, !range !229, !noalias !259
  %_22.i.i = icmp ne i8 %_23.i.i, %_11
  %_12.i.i = icmp ne i8 %9, 1
  %or.cond10.not.i = select i1 %_12.i.i, i1 true, i1 %_22.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !259
  %..i = sext i1 %or.cond10.not.i to i64
  br label %bb6.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb8.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !259
  %switch.tableidx = add nsw i8 %6, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6.i

bb6.i:                                            ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb4.i.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i
  %_0.sroa.0.0.i = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i ], [ %..i, %bb4.i.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i ]
  call void @llvm.experimental.noalias.scope.decl(metadata !271)
  call void @llvm.experimental.noalias.scope.decl(metadata !274)
  call void @llvm.experimental.noalias.scope.decl(metadata !277)
  call void @llvm.experimental.noalias.scope.decl(metadata !280)
  call void @llvm.experimental.noalias.scope.decl(metadata !283)
  %_9.i.i.i.i.i.i = load i64, ptr %_5.i, align 8, !range !123, !alias.scope !286, !noalias !259, !noundef !4
  %11 = icmp eq i64 %_9.i.i.i.i.i.i, 0
  br i1 %11, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit, label %bb6.i.i.i.i.i.i

bb6.i.i.i.i.i.i:                                  ; preds = %bb6.i
  %_10.i.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i.i, 24
  %_19.i.i.i.i.i.i = load ptr, ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !alias.scope !286, !noalias !259, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !286
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit

_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit: ; preds = %bb6.i.i.i.i.i.i, %bb6.i
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5.i), !noalias !259
  br label %bb5

bb5:                                              ; preds = %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit, %start
  %_0.sroa.0.0 = phi i64 [ %_0.sroa.0.0.i, %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_.exit ], [ -7, %start ]
  ret i64 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable
define noundef range(i64 -5, -38654705143) i64 @unir_edge_len(i32 noundef %capacity, i32 noundef %slot_size) unnamed_addr #23 {
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
define noundef range(i64 -8, 4294967288) i64 @unir_edge_max_payload(i32 noundef %capacity, i32 noundef %slot_size) unnamed_addr #23 {
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
define noundef i64 @unir_join(ptr noundef %vat, i64 noundef %child) unnamed_addr #1 {
start:
  %_6 = alloca [16 x i8], align 8
  %or.cond = icmp ugt i64 %child, 4294967295
  br i1 %or.cond, label %bb2, label %bb5

bb5:                                              ; preds = %start
  %_9 = trunc nuw i64 %child to i32
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_6)
  call void @_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat4join(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_6, ptr noalias noundef align 8 dereferenceable(88) %vat, i32 noundef %_9) #22
  %0 = load i8, ptr %_6, align 8, !range !125, !noundef !4
  %1 = trunc nuw i8 %0 to i1
  %2 = getelementptr inbounds nuw i8, ptr %_6, i64 8
  %3 = load i64, ptr %2, align 8
  %_0.sroa.0.0 = select i1 %1, i64 -6, i64 %3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_6)
  br label %bb2

bb2:                                              ; preds = %bb5, %start
  %_0.sroa.0.1 = phi i64 [ %_0.sroa.0.0, %bb5 ], [ -7, %start ]
  ret i64 %_0.sroa.0.1
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
  tail call void @llvm.experimental.noalias.scope.decl(metadata !287)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i)
  %3 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %4 = load i8, ptr %3, align 4, !range !101, !alias.scope !287, !noalias !290, !noundef !4
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb8.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3: ; preds = %start
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i)
  br label %bb6

bb8.i:                                            ; preds = %start
  %_13 = getelementptr inbounds nuw i8, ptr %p, i64 32
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_13, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5, i8 noundef 11, i8 noundef 2, i8 undef) #22, !noalias !293
  %5 = load i8, ptr %err.i, align 8, !range !101, !noalias !294, !noundef !4
  %6 = icmp eq i8 %5, 0
  br i1 %6, label %bb4.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb4.i:                                            ; preds = %bb8.i
  %7 = getelementptr inbounds nuw i8, ptr %err.i, i64 1
  %8 = load i8, ptr %7, align 1, !range !107, !noalias !294, !noundef !4
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
  call void @llvm.experimental.noalias.scope.decl(metadata !295)
  call void @llvm.experimental.noalias.scope.decl(metadata !298)
  call void @llvm.experimental.noalias.scope.decl(metadata !301)
  call void @llvm.experimental.noalias.scope.decl(metadata !304)
  call void @llvm.experimental.noalias.scope.decl(metadata !307)
  %_9.i.i.i.i.i = load i64, ptr %_5, align 8, !range !123, !alias.scope !310, !noundef !4
  %9 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %9, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb6
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_11.sroa.4.0._5.sroa_idx, align 8, !alias.scope !310, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !310
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb6
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: read) uwtable
define noundef range(i32 0, 1024) i32 @unir_producer_ended(ptr noundef readonly captures(none) %p) unnamed_addr #20 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %1 = load i8, ptr %0, align 4, !range !101, !noundef !4
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
  tail call void @unir_host_free(ptr noundef nonnull %p, i64 noundef 72, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !311
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
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_9.i), !noalias !316
  %or.cond.i = icmp ugt i64 %0, 4294967295
  br i1 %or.cond.i, label %bb11.i, label %bb12.i

bb11.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !316
  br label %bb2

bb12.i:                                           ; preds = %bb9.i
  %6 = icmp ne ptr %vat, null
  tail call void @llvm.assume(i1 %6)
  %_25.i = trunc nuw i64 %0 to i32
  call void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr noalias noundef nonnull sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_9.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i32 noundef %_25.i) #22, !noalias !316
  %7 = load i8, ptr %_9.i, align 8, !range !125, !noalias !316, !noundef !4
  %8 = trunc nuw i8 %7 to i1
  br i1 %8, label %bb13.i, label %bb14.i

bb13.i:                                           ; preds = %bb12.i
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !316
  br label %bb2

bb14.i:                                           ; preds = %bb12.i
  %9 = getelementptr inbounds nuw i8, ptr %_9.i, i64 8
  %_29.0.i = load i64, ptr %9, align 8, !noalias !316, !noundef !4
  %10 = getelementptr inbounds nuw i8, ptr %_9.i, i64 16
  %_29.1.i = load i64, ptr %10, align 8, !noalias !316, !noundef !4
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_9.i), !noalias !316
  %_12.i.i = zext i32 %1 to i64
  %_14.i.i = zext i32 %2 to i64
  %_11.i.i = mul nuw i64 %_14.i.i, %_12.i.i
  %_6.i.i = add nuw i64 %_11.i.i, 512
  %_3.i.i = icmp ult i64 %_29.1.i, %_6.i.i
  br i1 %_3.i.i, label %bb2, label %bb5

bb5:                                              ; preds = %bb14.i
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22
  %_0.i.i.i = tail call noalias noundef ptr @unir_host_alloc(i64 noundef 72, i64 noundef range(i64 1, -9223372036854775807) 8) #22
  %11 = icmp eq ptr %_0.i.i.i, null
  br i1 %11, label %bb6, label %bb7, !prof !147

bb2:                                              ; preds = %bb7, %bb14.i, %bb13.i, %bb11.i, %start
  %_0.sroa.0.0 = phi ptr [ %_0.i.i.i, %bb7 ], [ null, %bb11.i ], [ null, %bb13.i ], [ null, %start ], [ null, %bb14.i ]
  ret ptr %_0.sroa.0.0

bb6:                                              ; preds = %bb5
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 72) #28
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
  %_11 = load i8, ptr %0, align 1, !range !229, !noundef !4
  tail call void @llvm.experimental.noalias.scope.decl(metadata !320)
  call void @llvm.lifetime.start.p0(i64 88, ptr nonnull %_5.i), !noalias !320
  %1 = getelementptr inbounds nuw i8, ptr %_5.i, i64 24
  %2 = getelementptr inbounds nuw i8, ptr %_5.i, i64 56
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(32) %1, i8 0, i64 32, i1 false), !noalias !320
  store i64 65536, ptr %2, align 8, !noalias !320
  %3 = getelementptr inbounds nuw i8, ptr %_5.i, i64 64
  store i64 0, ptr %_5.i, align 8, !noalias !320
  %_12.sroa.4.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 8
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(24) %3, i8 0, i64 24, i1 false), !noalias !320
  store ptr inttoptr (i64 8 to ptr), ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !noalias !320
  %_12.sroa.5.0._5.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_5.i, i64 16
  store i64 0, ptr %_12.sroa.5.0._5.sroa_idx.i, align 8, !noalias !320
  tail call void @llvm.experimental.noalias.scope.decl(metadata !323)
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %err.i.i), !noalias !320
  %4 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %5 = load i8, ptr %4, align 4, !range !101, !alias.scope !326, !noalias !327, !noundef !4
  %.not.i.i = icmp eq i8 %5, 4
  br i1 %.not.i.i, label %bb8.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i: ; preds = %bb3
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !320
  br label %bb6.i

bb8.i.i:                                          ; preds = %bb3
  %_14.i = getelementptr inbounds nuw i8, ptr %p, i64 32
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(none) dereferenceable(16) %err.i.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_14.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_5.i, i8 noundef range(i8 0, 10) %_11, i8 noundef 0, i8 range(i8 0, 10) %_11) #22, !noalias !330
  %6 = load i8, ptr %err.i.i, align 8, !range !101, !noalias !331, !noundef !4
  %7 = icmp eq i8 %6, 0
  br i1 %7, label %bb4.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb8.i.i
  %8 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 1
  %9 = load i8, ptr %8, align 1, !range !107, !noalias !331, !noundef !4
  %10 = getelementptr inbounds nuw i8, ptr %err.i.i, i64 2
  %_23.i.i = load i8, ptr %10, align 2, !range !229, !noalias !320
  %_22.i.i = icmp ne i8 %_23.i.i, %_11
  %11 = icmp ne i8 %9, 0
  %or.cond9.not.i = select i1 %11, i1 true, i1 %_22.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !320
  %..i = sext i1 %or.cond9.not.i to i64
  br label %bb6.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb8.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %err.i.i), !noalias !320
  %switch.tableidx = add nsw i8 %6, -1
  %switch.idx.cast = zext i8 %switch.tableidx to i64
  %switch.offset = sub nuw nsw i64 -2, %switch.idx.cast
  br label %bb6.i

bb6.i:                                            ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb4.i.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i
  %_0.sroa.0.0.i = phi i64 [ -1, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread3.i ], [ %..i, %bb4.i.i ], [ %switch.offset, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i ]
  call void @llvm.experimental.noalias.scope.decl(metadata !332)
  call void @llvm.experimental.noalias.scope.decl(metadata !335)
  call void @llvm.experimental.noalias.scope.decl(metadata !338)
  call void @llvm.experimental.noalias.scope.decl(metadata !341)
  call void @llvm.experimental.noalias.scope.decl(metadata !344)
  %_9.i.i.i.i.i.i = load i64, ptr %_5.i, align 8, !range !123, !alias.scope !347, !noalias !320, !noundef !4
  %12 = icmp eq i64 %_9.i.i.i.i.i.i, 0
  br i1 %12, label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit, label %bb6.i.i.i.i.i.i

bb6.i.i.i.i.i.i:                                  ; preds = %bb6.i
  %_10.i.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i.i, 24
  %_19.i.i.i.i.i.i = load ptr, ptr %_12.sroa.4.0._5.sroa_idx.i, align 8, !alias.scope !347, !noalias !320, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !347
  br label %_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit

_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_.exit: ; preds = %bb6.i.i.i.i.i.i, %bb6.i
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_5.i), !noalias !320
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
  call void @llvm.experimental.noalias.scope.decl(metadata !348)
  %6 = getelementptr inbounds nuw i8, ptr %_7.i, i64 1
  %_0.i.i.i = getelementptr inbounds nuw i8, ptr %p, i64 32
  %7 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %8 = getelementptr inbounds nuw i8, ptr %p, i64 60
  %9 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  br label %bb26.i

bb27.i:                                           ; preds = %bb5.i
  %10 = load i8, ptr %7, align 2, !range !125, !alias.scope !351, !noalias !354, !noundef !4
  %_8.i.i = load i32, ptr %8, align 4, !alias.scope !351, !noalias !354, !noundef !4
  %_7.i.i = and i32 %_8.i.i, 1
  %_6.i.not.i = icmp eq i32 %_7.i.i, 0
  br i1 %_6.i.not.i, label %bb5.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

bb5.i.i:                                          ; preds = %bb27.i
  %_9.i.i = or disjoint i32 %_8.i.i, 1
  store i32 %_9.i.i, ptr %8, align 4, !alias.scope !351, !noalias !354
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb5.i.i, %bb27.i
  %_12.i.i = phi i32 [ %_8.i.i, %bb27.i ], [ %_9.i.i, %bb5.i.i ]
  %11 = trunc nuw i8 %10 to i1
  %..i.i = select i1 %11, i64 388, i64 260
  %_0.i.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i, i32 noundef %_12.i.i) #22, !noalias !358
  %.not.i = icmp eq i8 %_0.i.i, 7
  br i1 %.not.i, label %bb32.i, label %bb9.i

bb26.i:                                           ; preds = %bb26.i.backedge, %start
  %iter.sroa.0.047.i = phi i64 [ 0, %start ], [ %iter.sroa.0.047.i.be, %bb26.i.backedge ]
  %_42.i = add nuw nsw i64 %iter.sroa.0.047.i, 1
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_7.i), !noalias !359
  call fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(address) dereferenceable(16) %_7.i, ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %_25, ptr noalias noundef nonnull align 8 dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #30, !noalias !360
  %12 = load i8, ptr %_7.i, align 8, !range !145, !noalias !359, !noundef !4
  %.not19.i = icmp eq i8 %12, 5
  br i1 %.not19.i, label %bb29.i, label %bb28.i

bb32.i:                                           ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i
  %..i = select i1 %11, i64 256, i64 384
  %13 = call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %_0.i.i.i, i64 noundef %..i) #22, !noalias !358
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
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select21.i, i8 noundef %10, i8 %switch.select21.i) #22, !noalias !361
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb10.i:                                           ; preds = %bb32.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_24.i), !noalias !359
  call fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef sret([16 x i8]) align 8 captures(address) dereferenceable(16) %_24.i, ptr noalias noundef nonnull readonly align 8 dereferenceable(24) %_25, ptr noalias noundef nonnull align 8 dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10) #30, !noalias !360
  %15 = load i8, ptr %_24.i, align 8, !range !145, !noalias !359, !noundef !4
  %.not16.i = icmp eq i8 %15, 5
  br i1 %.not16.i, label %bb43.i, label %bb42.i

bb42.i:                                           ; preds = %bb10.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_24.i), !noalias !359
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit

bb43.i:                                           ; preds = %bb10.i
  %16 = load i8, ptr %9, align 1, !range !125, !noalias !359, !noundef !4
  %_60.i = trunc nuw i8 %16 to i1
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_24.i), !noalias !359
  br i1 %_60.i, label %bb13.i, label %bb12.i

bb13.i:                                           ; preds = %bb43.i
  %17 = call fastcc noundef i8 @_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, i1 noundef zeroext false) #22, !noalias !358
  %.not18.i = icmp eq i8 %17, 7
  br i1 %.not18.i, label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread, label %bb16.i

bb12.i:                                           ; preds = %bb43.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_38.i), !noalias !359
  call void @llvm.experimental.noalias.scope.decl(metadata !362)
  call void @llvm.experimental.noalias.scope.decl(metadata !365)
  %18 = load i8, ptr %7, align 2, !range !125, !alias.scope !367, !noalias !368, !noundef !4
  %19 = trunc nuw i8 %18 to i1
  %..i28.i = select i1 %19, i64 256, i64 384
  %20 = call { i1, i8 } @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate4wait(ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i28.i, i32 noundef %.sroa.6.0.extract.trunc.i, i64 noundef range(i64 0, 2) %_12.sroa.0.0, i64 %timeout_ns) #22, !noalias !370
  %21 = extractvalue { i1, i8 } %20, 0
  %22 = extractvalue { i1, i8 } %20, 1
  br i1 %21, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, label %bb13.i.i

bb13.i.i:                                         ; preds = %bb12.i
  %_8.i.i.i = load i32, ptr %8, align 4, !alias.scope !371, !noalias !368, !noundef !4
  %_7.i.i.i = and i32 %_8.i.i.i, 1
  %_6.i.not.i.i = icmp eq i32 %_7.i.i.i, 0
  br i1 %_6.i.not.i.i, label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, label %bb5.i.i.i

bb5.i.i.i:                                        ; preds = %bb13.i.i
  %_9.i.i.i = add i32 %_8.i.i.i, 1
  store i32 %_9.i.i.i, ptr %8, align 4, !alias.scope !371, !noalias !368
  br label %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i

_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i: ; preds = %bb5.i.i.i, %bb13.i.i
  %_12.i.i.i = phi i32 [ %_8.i.i.i, %bb13.i.i ], [ %_9.i.i.i, %bb5.i.i.i ]
  %..i.i.i = select i1 %19, i64 388, i64 260
  %_0.i.i29.i = call noundef range(i8 0, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %_0.i.i.i, i64 noundef %..i.i.i, i32 noundef %_12.i.i.i) #22, !noalias !370
  %.not.not.i.i = icmp eq i8 %_0.i.i29.i, 7
  br i1 %.not.not.i.i, label %bb6.i.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i
  %switch.i.i = icmp ult i8 %22, 2
  br i1 %switch.i.i, label %bb50.i, label %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i: ; preds = %bb6.i.i
  store i8 1, ptr %_38.i, align 8, !alias.scope !362, !noalias !374
  br label %bb49.i

_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i, %bb12.i
  %waited.sroa.4.0.i.i = phi i8 [ %_0.i.i29.i, %_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi.exit.i.i ], [ %22, %bb12.i ]
  %switch.selectcmp.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %waited.sroa.4.0.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_38.i, ptr noalias noundef nonnull align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select8.i.i, i8 noundef %18, i8 %switch.select8.i.i) #22, !noalias !358
  %.pr.i = load i8, ptr %_38.i, align 8, !noalias !359
  %.not17.i = icmp eq i8 %.pr.i, 5
  br i1 %.not17.i, label %bb50.i, label %bb49.i

bb16.i:                                           ; preds = %bb13.i
  %switch.selectcmp22.i = icmp eq i8 %17, 4
  %switch.select23.i = select i1 %switch.selectcmp22.i, i8 8, i8 2
  %switch.selectcmp24.i = icmp eq i8 %17, 3
  %switch.select25.i = select i1 %switch.selectcmp24.i, i8 6, i8 %switch.select23.i
  %23 = load i8, ptr %7, align 2, !range !125, !alias.scope !348, !noalias !354, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8, ptr noalias noundef align 8 dereferenceable(40) %_0.i.i.i, ptr noalias noundef nonnull align 8 dereferenceable(88) %_10, i8 noundef %switch.select25.i, i8 noundef %23, i8 %switch.select25.i) #22, !noalias !361
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb49.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread31.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_8, ptr noundef nonnull align 8 dereferenceable(16) %_38.i, i64 16, i1 false), !noalias !375
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !359
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exitthread-pre-split

bb50.i:                                           ; preds = %_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i, %bb6.i.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_38.i), !noalias !359
  br label %bb26.i.backedge

bb28.i:                                           ; preds = %bb26.i
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_7.i), !noalias !359
  br label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit

bb29.i:                                           ; preds = %bb26.i
  %24 = load i8, ptr %6, align 1, !range !125, !noalias !359, !noundef !4
  %_45.i = trunc nuw i8 %24 to i1
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_7.i), !noalias !359
  br i1 %_45.i, label %_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi.exit.thread, label %bb5.i

bb5.i:                                            ; preds = %bb29.i
  call void @llvm.x86.sse2.pause(), !noalias !358
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
  call void @llvm.experimental.noalias.scope.decl(metadata !376)
  call void @llvm.experimental.noalias.scope.decl(metadata !379)
  call void @llvm.experimental.noalias.scope.decl(metadata !382)
  call void @llvm.experimental.noalias.scope.decl(metadata !385)
  call void @llvm.experimental.noalias.scope.decl(metadata !388)
  %_9.i.i.i.i.i = load i64, ptr %_10, align 8, !range !123, !alias.scope !391, !noundef !4
  %27 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %27, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb12
  %_10.i.i.i.i.i = mul nuw i64 %_9.i.i.i.i.i, 24
  %_19.i.i.i.i.i = load ptr, ptr %_20.sroa.4.0._10.sroa_idx, align 8, !alias.scope !391, !nonnull !4, !noundef !4
  call void @unir_host_free(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 24, 0) %_10.i.i.i.i.i, i64 noundef range(i64 1, -9223372036854775807) 8) #22, !noalias !391
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb6.i.i.i.i.i, %bb12
  call void @llvm.lifetime.end.p0(i64 88, ptr nonnull %_10)
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8)
  ret i64 %_0.sroa.0.0
}

; Function Attrs: inlinehint nounwind nonlazybind uwtable
define internal fastcc void @_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE5writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(24) %_1, ptr noalias noundef nonnull align 8 captures(address, read_provenance) dereferenceable(72) %p, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) unnamed_addr #21 {
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
  %_10 = load ptr, ptr %_1, align 8, !nonnull !4, !align !392, !noundef !4
  %_7 = load i16, ptr %_10, align 2, !noundef !4
  %0 = getelementptr inbounds nuw i8, ptr %_1, i64 8
  %_11.0 = load ptr, ptr %0, align 8, !nonnull !4, !align !5, !noundef !4
  %1 = getelementptr inbounds nuw i8, ptr %_1, i64 16
  %_11.1 = load i64, ptr %1, align 8, !noundef !4
  tail call void @llvm.experimental.noalias.scope.decl(metadata !393)
  tail call void @llvm.experimental.noalias.scope.decl(metadata !396)
  %2 = getelementptr inbounds nuw i8, ptr %p, i64 32
  %3 = getelementptr inbounds nuw i8, ptr %p, i64 68
  %4 = load i8, ptr %3, align 4, !range !101, !alias.scope !396, !noalias !398, !noundef !4
  %.not.i = icmp eq i8 %4, 4
  br i1 %.not.i, label %bb20.i, label %bb21.i

bb21.i:                                           ; preds = %start
  %5 = getelementptr inbounds nuw i8, ptr %p, i64 69
  %_49.1.i = load i8, ptr %5, align 1, !alias.scope !396, !noalias !398
  %_53.sroa.3.0._0.sroa_idx.i = getelementptr inbounds nuw i8, ptr %_6, i64 2
  store i8 %_49.1.i, ptr %_53.sroa.3.0._0.sroa_idx.i, align 2, !alias.scope !393, !noalias !401
  br label %bb4

bb20.i:                                           ; preds = %start
  %6 = getelementptr inbounds nuw i8, ptr %p, i64 48
  %7 = getelementptr inbounds nuw i8, ptr %p, i64 52
  %_55.i = load i32, ptr %7, align 4, !alias.scope !396, !noalias !398, !noundef !4
  %_54.i = zext i32 %_55.i to i64
  %_10.i = add nsw i64 %_54.i, -8
  %_8.i = icmp ugt i64 %_11.1, %_10.i
  br i1 %_8.i, label %bb4, label %bb3.i

bb3.i:                                            ; preds = %bb20.i
  %_13.i = load i64, ptr %p, align 8, !alias.scope !396, !noalias !398, !noundef !4
  %p.i = add i64 %_13.i, 1
  %8 = getelementptr inbounds nuw i8, ptr %p, i64 16
  %_56.i = load i64, ptr %8, align 8, !alias.scope !396, !noalias !398, !noundef !4
  %9 = getelementptr inbounds nuw i8, ptr %p, i64 8
  %_57.i = load i64, ptr %9, align 8, !alias.scope !396, !noalias !398, !noundef !4
  %_59.i = load i32, ptr %6, align 8, !alias.scope !396, !noalias !398, !noundef !4
  %_58.i = zext i32 %_59.i to i64
  %10 = add i64 %_57.i, %_58.i
  %spec.store.select.i = tail call i64 @llvm.umin.i64(i64 %10, i64 %_56.i)
  %_14.i = icmp ugt i64 %p.i, %spec.store.select.i
  br i1 %_14.i, label %bb4.i, label %bb9.i

bb4.i:                                            ; preds = %bb3.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_17.i), !noalias !402
  tail call void @llvm.experimental.noalias.scope.decl(metadata !403)
  %11 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 144) #22, !noalias !406
  %.sroa.6.0.extract.shift.i.i.i = lshr i64 %11, 32
  %.sroa.6.0.extract.trunc.i.i.i = trunc nuw i64 %.sroa.6.0.extract.shift.i.i.i to i32
  %12 = trunc i64 %11 to i1
  br i1 %12, label %bb8.i.i.i, label %bb9.i.i.i

bb8.i.i.i:                                        ; preds = %bb4.i
  %.sroa.4.0.extract.shift.i.i.i = lshr i64 %11, 8
  %.sroa.4.0.extract.trunc.i.i.i = trunc i64 %.sroa.4.0.extract.shift.i.i.i to i8
  br label %bb3.i.i

bb9.i.i.i:                                        ; preds = %bb4.i
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !412
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_8.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 128) #22, !noalias !406
  %13 = load i8, ptr %_8.i.i.i, align 8, !range !125, !noalias !412, !noundef !4
  %14 = trunc nuw i8 %13 to i1
  br i1 %14, label %bb10.i.i.i, label %bb11.i.i.i

bb10.i.i.i:                                       ; preds = %bb9.i.i.i
  %15 = getelementptr inbounds nuw i8, ptr %_8.i.i.i, i64 1
  %_25.i.i.i = load i8, ptr %15, align 1, !range !225, !noalias !412, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !412
  br label %bb3.i.i

bb11.i.i.i:                                       ; preds = %bb9.i.i.i
  %16 = getelementptr inbounds nuw i8, ptr %_8.i.i.i, i64 8
  %_24.i.i.i = load i64, ptr %16, align 8, !noalias !412, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_8.i.i.i), !noalias !412
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !412
  call void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_12.i.i.i, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 136) #22, !noalias !406
  %17 = load i8, ptr %_12.i.i.i, align 8, !range !125, !noalias !412, !noundef !4
  %18 = trunc nuw i8 %17 to i1
  br i1 %18, label %bb12.i.i.i, label %bb4.i.i

bb12.i.i.i:                                       ; preds = %bb11.i.i.i
  %19 = getelementptr inbounds nuw i8, ptr %_12.i.i.i, i64 1
  %_30.i.i.i = load i8, ptr %19, align 1, !range !225, !noalias !412, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !412
  br label %bb3.i.i

bb3.i.i:                                          ; preds = %bb12.i.i.i, %bb10.i.i.i, %bb8.i.i.i
  %_4.sroa.5.0.ph.i.i = phi i8 [ %_30.i.i.i, %bb12.i.i.i ], [ %_25.i.i.i, %bb10.i.i.i ], [ %.sroa.4.0.extract.trunc.i.i.i, %bb8.i.i.i ]
  %switch.selectcmp.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i, 4
  %switch.select.i.i = select i1 %switch.selectcmp.i.i, i8 8, i8 2
  %switch.selectcmp7.i.i = icmp eq i8 %_4.sroa.5.0.ph.i.i, 3
  %switch.select8.i.i = select i1 %switch.selectcmp7.i.i, i8 6, i8 %switch.select.i.i
  %20 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %21 = load i8, ptr %20, align 2, !range !125, !alias.scope !413, !noalias !414, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select8.i.i, i8 noundef %21, i8 %switch.select8.i.i) #22, !noalias !415
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb4.i.i:                                          ; preds = %bb11.i.i.i
  %22 = getelementptr inbounds nuw i8, ptr %_12.i.i.i, i64 8
  %_29.i.i.i = load i64, ptr %22, align 8, !noalias !412, !noundef !4
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_12.i.i.i), !noalias !412
  %23 = tail call { i1, i8 } @_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal15decode_consumer(i32 noundef %.sroa.6.0.extract.trunc.i.i.i) #22, !noalias !414
  %24 = extractvalue { i1, i8 } %23, 0
  %25 = extractvalue { i1, i8 } %23, 1
  br i1 %24, label %bb9.i.i, label %bb6.i.i

bb9.i.i:                                          ; preds = %bb4.i.i
  %26 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %27 = load i8, ptr %26, align 2, !range !125, !alias.scope !413, !noalias !414, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 3, i8 noundef %27, i8 3) #22, !noalias !415
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb6.i.i:                                          ; preds = %bb4.i.i
  %28 = icmp ult i8 %25, 10
  %29 = icmp eq i8 %25, 12
  %30 = or i1 %28, %29
  br i1 %30, label %bb8.i.i, label %bb7.i.i

bb8.i.i:                                          ; preds = %bb6.i.i
  tail call void @llvm.assume(i1 %28)
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %25, i8 noundef 1, i8 %25) #22, !noalias !415
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

bb7.i.i:                                          ; preds = %bb6.i.i
  %_20.i.i = icmp eq i64 %_24.i.i.i, -1
  br i1 %_20.i.i, label %bb11.i.i, label %bb12.i.i

bb12.i.i:                                         ; preds = %bb7.i.i
  %_38.i.i = icmp ult i64 %_24.i.i.i, %_57.i
  br i1 %_38.i.i, label %bb34.i.i, label %bb25.i.i

bb11.i.i:                                         ; preds = %bb7.i.i
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef 11, i8 noundef 3, i8 undef) #22, !noalias !415
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
  store i64 %_24.i.i.i, ptr %9, align 8, !alias.scope !413, !noalias !414
  store i64 %_29.i.i.i, ptr %8, align 8, !alias.scope !413, !noalias !414
  br label %bb26.i

bb34.i.i:                                         ; preds = %bb35.i.i, %bb25.i.i, %bb12.i.i
  %checked.sroa.0.0.i.i = phi i8 [ 4, %bb12.i.i ], [ 3, %bb25.i.i ], [ 4, %bb35.i.i ]
  %31 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %32 = load i8, ptr %31, align 2, !range !125, !alias.scope !413, !noalias !414, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_17.i, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %checked.sroa.0.0.i.i, i8 noundef %32, i8 %checked.sroa.0.0.i.i) #22, !noalias !415
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb34.i.i, %bb11.i.i, %bb8.i.i, %bb9.i.i, %bb3.i.i
  %.pr.i = load i8, ptr %_17.i, align 8, !noalias !402
  %.not16.i = icmp eq i8 %.pr.i, 5
  br i1 %.not16.i, label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i, label %bb25.i

_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i: ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  %_64.pre.i = load i64, ptr %8, align 8, !alias.scope !396, !noalias !398
  %_65.pre.i = load i64, ptr %9, align 8, !alias.scope !396, !noalias !398
  %_67.pre.i = load i32, ptr %6, align 8, !alias.scope !396, !noalias !398
  %.pre.i = zext i32 %_67.pre.i to i64
  br label %bb26.i

bb9.i:                                            ; preds = %bb26.i, %bb3.i
  %_97.i = phi i32 [ %_67.i, %bb26.i ], [ %_59.i, %bb3.i ]
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %header.i), !noalias !402
  %_22.i = trunc i64 %_11.1 to i32
  store i32 %_22.i, ptr %header.i, align 4, !noalias !402
  %33 = getelementptr inbounds nuw i8, ptr %header.i, i64 4
  store i16 %_7, ptr %33, align 4, !noalias !402
  %34 = getelementptr inbounds nuw i8, ptr %header.i, i64 6
  store i16 0, ptr %34, align 2, !noalias !402
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %header2.i), !noalias !402
  call void @llvm.lifetime.start.p0(i64 9, ptr nonnull %_24.i), !noalias !402
  call void @_RNvMs0_CsdT7xHiCjqae_9unir_wireNtB5_11FrameHeader6encode(ptr noalias noundef nonnull sret([9 x i8]) align 1 captures(none) dereferenceable(9) %_24.i, ptr noalias noundef nonnull readonly align 4 captures(address, read_provenance) dereferenceable(8) %header.i) #22, !noalias !415
  %35 = load i8, ptr %_24.i, align 1, !range !125, !noalias !402, !noundef !4
  %36 = trunc nuw i8 %35 to i1
  br i1 %36, label %bb30.i, label %bb31.i, !prof !147

bb25.i:                                           ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.i
  call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_6, ptr noundef nonnull align 8 dereferenceable(16) %_17.i, i64 16, i1 false), !noalias !401
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_17.i), !noalias !402
  br label %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit

bb26.i:                                           ; preds = %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i
  %_66.pre-phi.i = phi i64 [ %.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_58.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_67.i = phi i32 [ %_67.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_59.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_65.i = phi i64 [ %_65.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_24.i.i.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  %_64.i = phi i64 [ %_64.pre.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.bb26_crit_edge.i ], [ %_29.i.i.i, %_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi.exit.thread.i ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %_17.i), !noalias !402
  %37 = add i64 %_65.i, %_66.pre-phi.i
  %spec.store.select4.i = tail call i64 @llvm.umin.i64(i64 %37, i64 %_64.i)
  %_19.i = icmp ugt i64 %p.i, %spec.store.select4.i
  br i1 %_19.i, label %bb5, label %bb9.i

bb30.i:                                           ; preds = %bb9.i
  call void @llvm.lifetime.start.p0(i64 1, ptr nonnull %_71.i), !noalias !402
  %38 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  %39 = load i8, ptr %38, align 1, !range !416, !noalias !402, !noundef !4
  store i8 %39, ptr %_71.i, align 1, !noalias !402
  call void @_RNvNtCscliFh4jUES5_4core6result13unwrap_failed(ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) @alloc_27f7ec91e8c14f79b1c7bc4a57a83efa, i64 noundef 19, ptr noundef nonnull align 1 %_71.i, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(32) @vtable.0.73, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.74) #29, !noalias !415
  unreachable

bb31.i:                                           ; preds = %bb9.i
  %40 = getelementptr inbounds nuw i8, ptr %_24.i, i64 1
  %41 = load i64, ptr %40, align 1, !noalias !402
  store i64 %41, ptr %header2.i, align 8, !noalias !402
  call void @llvm.lifetime.end.p0(i64 9, ptr nonnull %_24.i), !noalias !402
  %42 = getelementptr inbounds nuw i8, ptr %p, i64 24
  %43 = load i64, ptr %42, align 8, !alias.scope !396, !noalias !398, !noundef !4
  %_76.i = and i64 %_11.1, 7
  %44 = icmp eq i64 %_76.i, 0
  %45 = sub nuw nsw i64 8, %_76.i
  %46 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %43, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %header2.i, i64 noundef 8) #22, !noalias !415
  %.not17.i = icmp eq i8 %46, 7
  br i1 %.not17.i, label %bb37.i, label %bb17.i

bb37.i:                                           ; preds = %bb31.i
  %_81.i = add i64 %43, 8
  %47 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %_81.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_11.0, i64 noundef range(i64 0, -9223372036854775808) %_11.1) #22, !noalias !393
  %.not18.i = icmp eq i8 %47, 7
  br i1 %.not18.i, label %bb43.i, label %bb17.i

bb43.i:                                           ; preds = %bb37.i
  br i1 %44, label %bb46.i, label %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i

_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i: ; preds = %bb43.i
  %_4.i.i = add nuw i64 %_11.1, 8
  %_3.i.i = add i64 %_4.i.i, %43
  %48 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef %_3.i.i, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) @alloc_85fc59111fd0cef7ef4093da3840b035, i64 noundef %45) #22, !noalias !417
  %.not19.i = icmp eq i8 %48, 7
  br i1 %.not19.i, label %bb46.i, label %bb17.i

bb46.i:                                           ; preds = %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i, %bb43.i
  %49 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %2, i64 noundef 0, i64 noundef %p.i) #22, !noalias !393
  %.not20.i = icmp eq i8 %49, 7
  br i1 %.not20.i, label %bb13.i, label %bb17.i

bb13.i:                                           ; preds = %bb46.i
  store i64 %p.i, ptr %p, align 8, !alias.scope !396, !noalias !398
  %_92.i = load i32, ptr %7, align 4, !alias.scope !396, !noalias !398, !noundef !4
  %_91.i = zext i32 %_92.i to i64
  %50 = add i64 %43, %_91.i
  %_96.i = zext i32 %_97.i to i64
  %_95.i = mul nuw i64 %_91.i, %_96.i
  %_94.i = add nuw i64 %_95.i, 512
  %_93.i = icmp eq i64 %50, %_94.i
  %spec.store.select5.i = select i1 %_93.i, i64 512, i64 %50
  store i64 %spec.store.select5.i, ptr %42, align 8, !alias.scope !396, !noalias !398
  %51 = call fastcc noundef i8 @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s) #22, !noalias !393
  %.not21.i = icmp eq i8 %51, 7
  br i1 %.not21.i, label %bb16.i, label %bb17.i

bb16.i:                                           ; preds = %bb13.i
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header2.i), !noalias !402
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header.i), !noalias !402
  br label %bb5

bb17.i:                                           ; preds = %bb13.i, %bb46.i, %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i, %bb37.i, %bb31.i
  %.sink11 = phi i8 [ %49, %bb46.i ], [ %48, %_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi.exit.i ], [ %47, %bb37.i ], [ %46, %bb31.i ], [ %51, %bb13.i ]
  %switch.selectcmp24.i = icmp eq i8 %.sink11, 4
  %switch.select25.i = select i1 %switch.selectcmp24.i, i8 8, i8 2
  %switch.selectcmp26.i = icmp eq i8 %.sink11, 3
  %switch.select27.i = select i1 %switch.selectcmp26.i, i8 6, i8 %switch.select25.i
  %52 = getelementptr inbounds nuw i8, ptr %p, i64 70
  %53 = load i8, ptr %52, align 2, !range !125, !alias.scope !396, !noalias !398, !noundef !4
  call fastcc void @_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE5closeNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi(ptr noalias noundef nonnull sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_6, ptr noalias noundef align 8 dereferenceable(40) %2, ptr noalias noundef nonnull align 8 dereferenceable(88) %s, i8 noundef %switch.select27.i, i8 noundef %53, i8 %switch.select27.i) #22
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header2.i), !noalias !402
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %header.i), !noalias !402
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
  %1 = load i8, ptr %0, align 2, !range !125, !noundef !4
  %2 = trunc nuw i8 %1 to i1
  %. = select i1 %2, i64 384, i64 256
  %.13 = select i1 %2, i64 260, i64 388
  %3 = tail call i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %.13) #22
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
  %_13 = load i32, ptr %7, align 8, !noundef !4
  %_12 = add i32 %_13, 1
  store i32 %_12, ptr %7, align 8
  %8 = tail call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef %_12) #22
  %.not = icmp eq i8 %8, 7
  br i1 %.not, label %bb19, label %bb15

bb19:                                             ; preds = %bb7
  %9 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate6notify(ptr noalias noundef nonnull align 8 dereferenceable(88) %s, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %., i32 noundef 1) #22
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
define internal noundef zeroext i1 @_RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt(ptr noalias noundef readonly align 1 captures(none) dereferenceable(1) %self, ptr noalias noundef align 8 dereferenceable(24) %f) unnamed_addr #21 {
start:
  %0 = load i8, ptr %self, align 1, !range !416, !noundef !4
  %1 = zext nneg i8 %0 to i64
  %switch.gep = getelementptr inbounds nuw [9 x i64], ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt, i64 0, i64 %1
  %switch.load = load i64, ptr %switch.gep, align 8
  %2 = zext nneg i8 %0 to i64
  %reltable.shift = shl i64 %2, 2
  %reltable.intrinsic = call ptr @llvm.load.relative.i64(ptr @switch.table._RNvXsi_CsdT7xHiCjqae_9unir_wireNtB5_9WireErrorNtNtCscliFh4jUES5_4core3fmt5Debug3fmt.1.rel, i64 %reltable.shift)
  %_0 = tail call noundef zeroext i1 @_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter9write_str(ptr noalias noundef nonnull align 8 dereferenceable(24) %f, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %reltable.intrinsic, i64 noundef %switch.load) #22
  ret i1 %_0
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -6, 4294967296) i64 @unir_region_create(ptr noundef nonnull %vat, i64 noundef %len) unnamed_addr #1 {
start:
  %0 = tail call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate13region_create(ptr noalias noundef nonnull align 8 dereferenceable(88) %vat, i64 noundef %len) #22
  %1 = trunc i64 %0 to i1
  %.sroa.5.0.extract.shift = lshr i64 %0, 32
  %_0.sroa.0.0 = select i1 %1, i64 -6, i64 %.sroa.5.0.extract.shift
  ret i64 %_0.sroa.0.0
}

; Function Attrs: nounwind nonlazybind uwtable
define noundef range(i64 -7, 4294967296) i64 @unir_spawn(ptr noundef %vat, ptr noundef readonly captures(none) %args, i64 noundef %args_len, ptr noundef captures(address, read_provenance) %name, i64 noundef %name_len, i64 noundef %cap, i64 noundef %fuel) unnamed_addr #1 {
start:
  %_23 = alloca [24 x i8], align 8
  %endowment = alloca [40 x i8], align 8
  %grants = alloca [24 x i8], align 8
  %_13 = alloca [24 x i8], align 8
  %0 = icmp eq i64 %args_len, 0
  %.args = select i1 %0, ptr inttoptr (i64 1 to ptr), ptr %args
  %1 = icmp eq i64 %name_len, 0
  %name2.sroa.0.0 = select i1 %1, ptr inttoptr (i64 1 to ptr), ptr %name
  call void @_RNvNtNtCscliFh4jUES5_4core3str8converts9from_utf8(ptr noalias noundef nonnull sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_13, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %name2.sroa.0.0, i64 noundef %name_len) #22
  %or.cond = icmp ugt i64 %cap, 4294967295
  %2 = load i64, ptr %_13, align 8, !range !148
  %3 = trunc nuw i64 %2 to i1
  %or.cond6 = select i1 %or.cond, i1 true, i1 %3
  br i1 %or.cond6, label %bb4, label %bb20

bb20:                                             ; preds = %start
  %_32 = trunc nuw i64 %cap to i32
  %4 = getelementptr inbounds nuw i8, ptr %_13, i64 8
  %name.0 = load ptr, ptr %4, align 8, !nonnull !4, !align !5, !noundef !4
  %5 = getelementptr inbounds nuw i8, ptr %_13, i64 16
  %name.1 = load i64, ptr %5, align 8, !noundef !4
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %grants)
  store ptr %name.0, ptr %grants, align 8
  %_17.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %grants, i64 8
  store i64 %name.1, ptr %_17.sroa.4.0..sroa_idx, align 8
  %_17.sroa.5.0..sroa_idx = getelementptr inbounds nuw i8, ptr %grants, i64 16
  store i32 %_32, ptr %_17.sroa.5.0..sroa_idx, align 8
  call void @llvm.lifetime.start.p0(i64 40, ptr nonnull %endowment)
  store ptr %grants, ptr %endowment, align 8
  %6 = getelementptr inbounds nuw i8, ptr %endowment, i64 8
  store i64 1, ptr %6, align 8
  %7 = getelementptr inbounds nuw i8, ptr %endowment, i64 16
  store i64 %fuel, ptr %7, align 8
  %_21.sroa.4.0..sroa_idx = getelementptr inbounds nuw i8, ptr %endowment, i64 24
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(16) %_21.sroa.4.0..sroa_idx, i8 0, i64 16, i1 false)
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_23)
  br i1 %0, label %_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi.exit, label %bb5.i.i

bb5.i.i:                                          ; preds = %bb20
  call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22, !noalias !420
  %_0.i.i.i.i.i.i = call noalias noundef ptr @unir_host_alloc(i64 noundef range(i64 1, -9223372036854775808) %args_len, i64 noundef range(i64 1, -9223372036854775807) 1) #22, !noalias !420
  %8 = icmp eq ptr %_0.i.i.i.i.i.i, null
  br i1 %8, label %bb3.i, label %bb10.i.i

bb10.i.i:                                         ; preds = %bb5.i.i
  %9 = ptrtoint ptr %_0.i.i.i.i.i.i to i64
  br label %_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi.exit

bb3.i:                                            ; preds = %bb5.i.i
  call void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec12handle_error(i64 noundef 1, i64 range(i64 0, -9223372036854775808) %args_len) #28, !noalias !426
  unreachable

_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi.exit: ; preds = %bb10.i.i, %bb20
  %_9.sroa.9.0.ph.i = phi i64 [ 1, %bb20 ], [ %9, %bb10.i.i ]
  %10 = inttoptr i64 %_9.sroa.9.0.ph.i to ptr
  call void @llvm.memcpy.p0.p0.i64(ptr nonnull align 1 %10, ptr nonnull readonly align 1 %.args, i64 range(i64 0, -9223372036854775808) %args_len, i1 false), !noalias !427
  store i64 %args_len, ptr %_23, align 8
  %_24.sroa.4.0._23.sroa_idx = getelementptr inbounds nuw i8, ptr %_23, i64 8
  store ptr %10, ptr %_24.sroa.4.0._23.sroa_idx, align 8
  %_24.sroa.5.0._23.sroa_idx = getelementptr inbounds nuw i8, ptr %_23, i64 16
  store i64 %args_len, ptr %_24.sroa.5.0._23.sroa_idx, align 8
  %11 = call i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate5spawn(ptr noalias noundef align 8 dereferenceable(88) %vat, ptr noalias noundef nonnull align 8 captures(address) dereferenceable(24) %_23, ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(40) %endowment) #22
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_23)
  %12 = trunc i64 %11 to i1
  %.sroa.5.0.extract.shift = lshr i64 %11, 32
  %_0.sroa.0.0 = select i1 %12, i64 -6, i64 %.sroa.5.0.extract.shift
  call void @llvm.lifetime.end.p0(i64 40, ptr nonnull %endowment)
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %grants)
  br label %bb4

bb4:                                              ; preds = %_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi.exit, %start
  %_0.sroa.0.1 = phi i64 [ %_0.sroa.0.0, %_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi.exit ], [ -7, %start ]
  ret i64 %_0.sroa.0.1
}

; Function Attrs: nounwind nonlazybind uwtable
define noalias noundef nonnull ptr @unir_vat_root(i64 noundef %map_base, i64 noundef %map_end, i64 noundef %carve_base, i32 noundef %carve_log2, i32 noundef %0) unnamed_addr #1 {
start:
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22
  %_0.i.i.i = tail call noalias noundef ptr @unir_host_alloc(i64 noundef 88, i64 noundef range(i64 1, -9223372036854775807) 8) #22
  %1 = icmp eq ptr %_0.i.i.i, null
  br i1 %1, label %bb5, label %bb6, !prof !147

bb5:                                              ; preds = %start
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc5alloc18handle_alloc_error(i64 noundef 8, i64 noundef 88) #28
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
declare i32 @llvm.umax.i32(i32, i32) #6

; Function Attrs: nounwind nonlazybind uwtable
define internal void @_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat4join(ptr dead_on_unwind noalias noundef writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) %_0, ptr noalias noundef readonly align 8 captures(none) dereferenceable(88) %self, i32 noundef %child) unnamed_addr #1 {
start:
  %ty.i.i.i.i = alloca [4 x i8], align 4
  %_23 = icmp sgt i32 %child, -1
  br i1 %_23, label %bb6, label %bb7

bb7:                                              ; preds = %start
  %_24 = and i32 %child, 2147483647
  %i = zext nneg i32 %_24 to i64
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_31 = load i64, ptr %0, align 8, !noundef !4
  %_34 = icmp ugt i64 %_31, %i
  br i1 %_34, label %bb8, label %bb10

bb6:                                              ; preds = %start
  %1 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 1, ptr %1, align 1
  br label %bb5

bb8:                                              ; preds = %bb7
  %2 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_32 = load ptr, ptr %2, align 8, !nonnull !4, !noundef !4
  %_35 = getelementptr inbounds nuw %"core::option::Option<(i64, u32, i32)>", ptr %_32, i64 %i
  %_11.sroa.0.0.copyload = load i64, ptr %_35, align 8
  %_11.sroa.5.0._36.sroa_idx = getelementptr inbounds nuw i8, ptr %_35, i64 8
  %_11.sroa.5.sroa.0.0.copyload = load i64, ptr %_11.sroa.5.0._36.sroa_idx, align 8
  %_11.sroa.5.sroa.5.0._11.sroa.5.0._36.sroa_idx.sroa_idx = getelementptr inbounds nuw i8, ptr %_35, i64 16
  %_11.sroa.5.sroa.5.0.copyload = load i32, ptr %_11.sroa.5.sroa.5.0._11.sroa.5.0._36.sroa_idx.sroa_idx, align 8
  %_11.sroa.5.sroa.6.0._11.sroa.5.0._36.sroa_idx.sroa_idx = getelementptr inbounds nuw i8, ptr %_35, i64 20
  %_11.sroa.5.sroa.6.0.copyload = load i32, ptr %_11.sroa.5.sroa.6.0._11.sroa.5.0._36.sroa_idx.sroa_idx, align 4
  store i64 0, ptr %_35, align 8
  %3 = trunc nuw i64 %_11.sroa.0.0.copyload to i1
  br i1 %3, label %bb11, label %bb10

bb10:                                             ; preds = %bb8, %bb7
  %4 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 0, ptr %4, align 1
  br label %bb5

bb11:                                             ; preds = %bb8
  %n.i = tail call noundef i32 @__vm_cap_count() #22
  %smax.i.i = tail call i32 @llvm.smax.i32(i32 %n.i, i32 0)
  br label %bb1.i.i

bb1.i.i:                                          ; preds = %bb3.i.i, %bb11
  %5 = phi i32 [ %6, %bb3.i.i ], [ 0, %bb11 ]
  %exitcond.not.i.not.not.not.not.i.not = icmp eq i32 %5, %smax.i.i
  br i1 %exitcond.not.i.not.not.not.not.i.not, label %bb12, label %bb3.i.i

bb3.i.i:                                          ; preds = %bb1.i.i
  %6 = add nuw i32 %5, 1
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %ty.i.i.i.i), !noalias !428
  store i32 -1, ptr %ty.i.i.i.i, align 4, !noalias !428
  %h.i.i.i.i = call noundef i32 @__vm_cap_at(i32 noundef %5, ptr noundef nonnull %ty.i.i.i.i) #22, !noalias !428
  %_7.i.i.i.i = load i32, ptr %ty.i.i.i.i, align 4, !noalias !428, !noundef !4
  %_6.i.i.i.i = icmp eq i32 %_7.i.i.i.i, 6
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %ty.i.i.i.i), !noalias !428
  br i1 %_6.i.i.i.i, label %bb13, label %bb1.i.i

bb5:                                              ; preds = %bb12, %bb13, %bb10, %bb6
  %.sink = phi i8 [ 1, %bb6 ], [ 0, %bb13 ], [ 1, %bb12 ], [ 1, %bb10 ]
  store i8 %.sink, ptr %_0, align 8
  ret void

bb13:                                             ; preds = %bb3.i.i
  %r = call noundef i64 @__vm_join(i32 noundef %h.i.i.i.i, i64 noundef %_11.sroa.5.sroa.0.0.copyload) #22
  %7 = getelementptr inbounds nuw i8, ptr %self, i64 24
  %_50 = load i64, ptr %7, align 8, !noundef !4
  %_52 = zext i32 %_11.sroa.5.sroa.5.0.copyload to i64
  %_51 = shl nuw nsw i64 %_52, 16
  %_22 = add i64 %_50, %_51
  %_4.i = call noundef i64 @__vm_region_unmap(i32 noundef %_11.sroa.5.sroa.6.0.copyload, i64 noundef %_22, i64 noundef 65536) #22
  %8 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %r, ptr %8, align 8
  br label %bb5

bb12:                                             ; preds = %bb1.i.i
  %9 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 6, ptr %9, align 1
  br label %bb5
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i32 @__vm_cap_count() unnamed_addr #1

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smax.i32(i32, i32) #6

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i32 @__vm_cap_at(i32 noundef, ptr noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_join(i32 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_region_unmap(i32 noundef, i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i32 @__vm_cap_resolve(ptr noundef, i64 noundef) unnamed_addr #1

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
  br i1 %or.cond19, label %bb6, label %bb12, !prof !431

bb12:                                             ; preds = %start
  %_56 = load i64, ptr %self, align 8, !noundef !4
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
  br i1 %epil.iter.cmp.not, label %bb17, label %bb18.epil, !llvm.loop !432

bb17:                                             ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa, %bb12
  %t = add i64 %words4.i, %spec.store.select.i
  %_99 = icmp ult i64 %t, %spec.store.select.i
  %_95.not = icmp ugt i64 %t, %dst.1
  %or.cond = or i1 %_99, %_95.not
  br i1 %or.cond, label %bb21, label %bb20, !prof !434

bb21:                                             ; preds = %bb17
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %spec.store.select.i, i64 noundef %t, i64 noundef %dst.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #29
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
  store i64 %12, ptr %iter2.sroa.0.024.epil, align 1, !alias.scope !435, !noalias !438
  %epil.iter31.next = add i64 %epil.iter31, 1
  %epil.iter31.cmp.not = icmp eq i64 %epil.iter31.next, %xtraiter30
  br i1 %epil.iter31.cmp.not, label %bb26, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil, !llvm.loop !440

bb26:                                             ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.epil, %bb26.loopexit.unr-lcssa, %bb20
  %_41 = add nuw i64 %t, %_9.i
  %_119.not = icmp ugt i64 %_41, %dst.1
  br i1 %_119.not, label %bb29, label %bb28, !prof !434

_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit: ; preds = %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new
  %iter3.sroa.0.026 = phi i64 [ 0, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %_105.0.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %iter2.sroa.0.024 = phi ptr [ %_76, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %_113.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %niter34 = phi i64 [ 0, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit.lr.ph.new ], [ %niter34.next.3, %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit ]
  %_113 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 8
  %_33 = shl i64 %iter3.sroa.0.026, 3
  %_31 = add i64 %_32, %_33
  %_30 = inttoptr i64 %_31 to ptr
  %13 = load volatile i64, ptr %_30, align 8
  store i64 %13, ptr %iter2.sroa.0.024, align 1, !alias.scope !435, !noalias !438
  %_113.1 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 16
  %_105.0 = shl i64 %iter3.sroa.0.026, 3
  %_33.1 = or disjoint i64 %_105.0, 8
  %_31.1 = add i64 %_32, %_33.1
  %_30.1 = inttoptr i64 %_31.1 to ptr
  %14 = load volatile i64, ptr %_30.1, align 8
  store i64 %14, ptr %_113, align 1, !alias.scope !435, !noalias !438
  %_113.2 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 24
  %_105.0.1 = shl i64 %iter3.sroa.0.026, 3
  %_33.2 = or disjoint i64 %_105.0.1, 16
  %_31.2 = add i64 %_32, %_33.2
  %_30.2 = inttoptr i64 %_31.2 to ptr
  %15 = load volatile i64, ptr %_30.2, align 8
  store i64 %15, ptr %_113.1, align 1, !alias.scope !435, !noalias !438
  %_113.3 = getelementptr inbounds nuw i8, ptr %iter2.sroa.0.024, i64 32
  %_105.0.3 = add nuw nsw i64 %iter3.sroa.0.026, 4
  %_105.0.2 = shl i64 %iter3.sroa.0.026, 3
  %_33.3 = or disjoint i64 %_105.0.2, 24
  %_31.3 = add i64 %_32, %_33.3
  %_30.3 = inttoptr i64 %_31.3 to ptr
  %16 = load volatile i64, ptr %_30.3, align 8
  store i64 %16, ptr %_113.2, align 1, !alias.scope !435, !noalias !438
  %niter34.next.3 = add i64 %niter34, 4
  %niter34.ncmp.3 = icmp eq i64 %niter34.next.3, %unroll_iter33
  br i1 %niter34.ncmp.3, label %bb26.loopexit.unr-lcssa, label %_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen.exit

bb29:                                             ; preds = %bb26
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %t, i64 noundef %_41, i64 noundef %dst.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #29
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

; Function Attrs: cold noinline nounwind nonlazybind uwtable
define internal void @_RNvMs3_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_E8grow_oneCsIEB7taFyf8_10unir_temen(ptr noalias noundef align 8 captures(none) dereferenceable(16) %self) unnamed_addr #24 {
start:
  %_5 = load i64, ptr %self, align 8, !range !123, !noundef !4
  %0 = tail call fastcc { i64, i64 } @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner14grow_amortizedCsIEB7taFyf8_10unir_temen(ptr noalias noundef align 8 dereferenceable(16) %self, i64 noundef %_5, i64 noundef 4, i64 noundef 16) #22
  %1 = extractvalue { i64, i64 } %0, 0
  %.not = icmp eq i64 %1, -9223372036854775807
  br i1 %.not, label %bb3, label %bb2, !prof !3

bb2:                                              ; preds = %start
  %2 = extractvalue { i64, i64 } %0, 1
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec12handle_error(i64 noundef %1, i64 %2) #28
  unreachable

bb3:                                              ; preds = %start
  ret void
}

; Function Attrs: cold nounwind nonlazybind uwtable
define internal fastcc { i64, i64 } @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner14grow_amortizedCsIEB7taFyf8_10unir_temen(ptr noalias noundef nonnull align 8 captures(none) dereferenceable(16) %self, i64 noundef range(i64 0, -9223372036854775808) %len, i64 noundef range(i64 4, 9) %elem_layout.0, i64 noundef range(i64 16, 25) %elem_layout.1) unnamed_addr #25 {
start:
  %_16 = alloca [24 x i8], align 8
  %_22.0 = add nuw i64 %len, 1
  %_13 = load i64, ptr %self, align 8, !range !123, !noundef !4
  %0 = shl nuw i64 %_13, 1
  %spec.store.select = tail call i64 @llvm.umax.i64(i64 %_22.0, i64 %0)
  %cap1.sroa.0.1 = tail call i64 @llvm.umax.i64(i64 %spec.store.select, i64 4)
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_16)
  call fastcc void @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner11finish_growCsIEB7taFyf8_10unir_temen(ptr noalias noundef sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_16, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(16) %self, i64 noundef %cap1.sroa.0.1, i64 noundef %elem_layout.0, i64 noundef %elem_layout.1) #22
  %_35 = load i64, ptr %_16, align 8, !range !148, !noundef !4
  %1 = trunc nuw i64 %_35 to i1
  %2 = getelementptr inbounds nuw i8, ptr %_16, i64 8
  br i1 %1, label %bb22, label %bb23

bb6:                                              ; preds = %bb23, %bb22
  %_0.sroa.5.0 = phi i64 [ %_37.1, %bb22 ], [ undef, %bb23 ]
  %_0.sroa.0.0 = phi i64 [ %_37.0, %bb22 ], [ -9223372036854775807, %bb23 ]
  %3 = insertvalue { i64, i64 } poison, i64 %_0.sroa.0.0, 0
  %4 = insertvalue { i64, i64 } %3, i64 %_0.sroa.5.0, 1
  ret { i64, i64 } %4

bb22:                                             ; preds = %start
  %_37.0 = load i64, ptr %2, align 8, !range !441, !noundef !4
  %5 = getelementptr inbounds nuw i8, ptr %_16, i64 16
  %_37.1 = load i64, ptr %5, align 8
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_16)
  br label %bb6

bb23:                                             ; preds = %start
  %_36.0 = load ptr, ptr %2, align 8, !nonnull !4, !noundef !4
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_16)
  %6 = getelementptr inbounds nuw i8, ptr %self, i64 8
  store ptr %_36.0, ptr %6, align 8
  %7 = icmp sgt i64 %cap1.sroa.0.1, -1
  tail call void @llvm.assume(i1 %7)
  store i64 %cap1.sroa.0.1, ptr %self, align 8
  br label %bb6
}

; Function Attrs: cold nounwind nonlazybind uwtable
define internal fastcc void @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner11finish_growCsIEB7taFyf8_10unir_temen(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) initializes((0, 8)) %_0, ptr noalias noundef nonnull readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef range(i64 4, -1) %cap, i64 noundef range(i64 4, 9) %elem_layout.0, i64 noundef range(i64 16, 25) %elem_layout.1) unnamed_addr #25 {
start:
  %_7.i = add nsw i64 %elem_layout.0, -1
  %_9.i = add nuw nsw i64 %_7.i, %elem_layout.1
  %_11.i = sub nsw i64 0, %elem_layout.0
  %padded.i = and i64 %_9.i, %_11.i
  %0 = tail call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %padded.i, i64 %cap)
  %_17.0.i = extractvalue { i64, i1 } %0, 0
  %_17.1.i = extractvalue { i64, i1 } %0, 1
  %_23.i = sub nuw i64 -9223372036854775808, %elem_layout.0
  %_22.i = icmp ugt i64 %_17.0.i, %_23.i
  %or.cond.i = select i1 %_17.1.i, i1 true, i1 %_22.i
  br i1 %or.cond.i, label %bb8, label %bb11, !prof !431

bb11:                                             ; preds = %start
  %_30 = load i64, ptr %self, align 8, !range !123, !noundef !4
  %1 = icmp eq i64 %_30, 0
  br i1 %1, label %bb14, label %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator4grow.exit

bb14:                                             ; preds = %bb11
  %2 = icmp eq i64 %_17.0.i, 0
  br i1 %2, label %bb4.thread, label %bb1.i.i

bb4.thread:                                       ; preds = %bb14
  %_14.i.i = inttoptr i64 %elem_layout.0 to ptr
  br label %bb6

bb1.i.i:                                          ; preds = %bb14
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22
  %3 = tail call noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc12___rust_alloc(i64 noundef %_17.0.i, i64 noundef range(i64 1, -9223372036854775807) %elem_layout.0) #22
  br label %bb4

_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator4grow.exit: ; preds = %bb11
  %_31 = mul nuw i64 %_30, %elem_layout.1
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_40 = load ptr, ptr %4, align 8, !nonnull !4, !noundef !4
  %_12.i.i = icmp uge i64 %_17.0.i, %_31
  tail call void @llvm.assume(i1 %_12.i.i)
  %raw_ptr.i.i = tail call noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_realloc(ptr noundef nonnull %_40, i64 noundef range(i64 16, 0) %_31, i64 noundef range(i64 4, 9) %elem_layout.0, i64 noundef %_17.0.i) #22
  br label %bb4

bb4:                                              ; preds = %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator4grow.exit, %bb1.i.i
  %_0.sroa.0.0.i.i.pn = phi ptr [ %raw_ptr.i.i, %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator4grow.exit ], [ %3, %bb1.i.i ]
  %5 = icmp eq ptr %_0.sroa.0.0.i.i.pn, null
  br i1 %5, label %bb5, label %bb6

bb5:                                              ; preds = %bb4
  %6 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %elem_layout.0, ptr %6, align 8
  br label %bb8

bb6:                                              ; preds = %bb4, %bb4.thread
  %_0.sroa.0.0.i.i.pn14 = phi ptr [ %_14.i.i, %bb4.thread ], [ %_0.sroa.0.0.i.i.pn, %bb4 ]
  %7 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store ptr %_0.sroa.0.0.i.i.pn14, ptr %7, align 8
  br label %bb8

bb8:                                              ; preds = %bb6, %bb5, %start
  %.sink15 = phi i64 [ 16, %bb6 ], [ 16, %bb5 ], [ 8, %start ]
  %_17.0.i.sink = phi i64 [ %_17.0.i, %bb6 ], [ %_17.0.i, %bb5 ], [ 0, %start ]
  %storemerge7 = phi i64 [ 0, %bb6 ], [ 1, %bb5 ], [ 1, %start ]
  %8 = getelementptr inbounds nuw i8, ptr %_0, i64 %.sink15
  store i64 %_17.0.i.sink, ptr %8, align 8
  store i64 %storemerge7, ptr %_0, align 8
  ret void
}

; Function Attrs: cold noinline nounwind nonlazybind uwtable
define internal void @_RNvMs3_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEE8grow_oneCsIEB7taFyf8_10unir_temen(ptr noalias noundef align 8 captures(none) dereferenceable(16) %self) unnamed_addr #24 {
start:
  %_5 = load i64, ptr %self, align 8, !range !123, !noundef !4
  %0 = tail call fastcc { i64, i64 } @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner14grow_amortizedCsIEB7taFyf8_10unir_temen(ptr noalias noundef align 8 dereferenceable(16) %self, i64 noundef %_5, i64 noundef 8, i64 noundef 24) #22
  %1 = extractvalue { i64, i64 } %0, 0
  %.not = icmp eq i64 %1, -9223372036854775807
  br i1 %.not, label %bb3, label %bb2, !prof !3

bb2:                                              ; preds = %start
  %2 = extractvalue { i64, i64 } %0, 1
  tail call void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec12handle_error(i64 noundef %1, i64 %2) #28
  unreachable

bb3:                                              ; preds = %start
  ret void
}

; Function Attrs: nounwind nonlazybind uwtable
define internal void @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate10region_map(ptr dead_on_unwind noalias noundef writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) initializes((0, 1)) %_0, ptr noalias noundef align 8 captures(none) dereferenceable(88) %self, i32 noundef %region) unnamed_addr #1 {
start:
  %_25 = icmp slt i32 %region, 0
  br i1 %_25, label %bb10, label %bb11

bb11:                                             ; preds = %start
  %n.i = tail call noundef i64 @__vm_region_call(i32 noundef %region, i32 noundef 2, i64 noundef 0, i64 noundef 0, i64 noundef 0, i64 noundef 0) #22, !noalias !442
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
  %_15 = load i64, ptr %3, align 8, !noundef !4
  %_14 = add i64 %_15, %span.sroa.0.0
  %4 = getelementptr inbounds nuw i8, ptr %self, i64 32
  %_17 = load i64, ptr %4, align 8, !noundef !4
  %_13 = icmp ugt i64 %_14, %_17
  br i1 %_13, label %bb3, label %bb4

bb4:                                              ; preds = %bb13
  %r.i = tail call noundef i64 @__vm_region_map(i32 noundef %region, i64 noundef %_15, i64 noundef 0, i64 noundef %span.sroa.0.0, i32 noundef 3) #22
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
  br i1 %_19.1, label %bb7, label %bb14, !prof !147

bb14:                                             ; preds = %bb8, %start
  %_5.sroa.5.0 = phi i64 [ %len, %start ], [ %_19.0, %bb8 ]
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 72
  %_10 = load i64, ptr %1, align 8, !noundef !4
  %_8 = icmp ugt i64 %_5.sroa.5.0, %_10
  br i1 %_8, label %bb7, label %bb2

bb2:                                              ; preds = %bb14
  %2 = tail call noundef i64 @__vm_region_create(i64 noundef %_5.sroa.5.0) #22
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
  br i1 %or.cond, label %bb7, label %bb9, !prof !431

bb9:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %region, align 8, !alias.scope !445, !noalias !448, !noundef !4
  %_13.i = add i64 %_14.i, %off
  %4 = trunc nuw i64 %0 to i1
  br i1 %4, label %bb11, label %bb10

bb11:                                             ; preds = %bb9
  %spec.store.select = tail call i64 @llvm.umin.i64(i64 %1, i64 9223372036854775807)
  %5 = tail call noundef i32 @__vm_wait32(i64 noundef %_13.i, i32 noundef %expected, i64 noundef %spec.store.select) #22
  %6 = icmp ugt i32 %5, 2
  %switch.idx.cast = trunc i32 %5 to i8
  %spec.select = select i1 %6, i8 6, i8 %switch.idx.cast
  br label %bb7

bb10:                                             ; preds = %bb9
  %7 = tail call noundef i32 @__vm_wait32(i64 noundef %_13.i, i32 noundef %expected, i64 noundef 1000000000) #22
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
define internal range(i64 0, -4294901760) i64 @_RNvXs1_CsIEB7taFyf8_10unir_temenNtB5_8TemenVatNtCsc1i3HueRnCr_14unir_substrate9Substrate5spawn(ptr noalias noundef align 8 captures(none) dereferenceable(88) %self, ptr dead_on_return noalias noundef readonly align 8 captures(none) dereferenceable(24) %program, ptr noalias noundef readonly align 8 captures(none) dereferenceable(40) %endowment) unnamed_addr #1 {
start:
  %ty.i.i.i.i = alloca [4 x i8], align 4
  %_133 = alloca [24 x i8], align 8
  %grants = alloca [24 x i8], align 8
  %0 = getelementptr inbounds nuw i8, ptr %endowment, i64 16
  %_94 = load i64, ptr %0, align 8, !noundef !4
  %1 = getelementptr inbounds nuw i8, ptr %self, i64 64
  %_95 = load i64, ptr %1, align 8, !noundef !4
  %_93.not = icmp ugt i64 %_94, %_95
  br i1 %_93.not, label %bb25, label %bb27

bb27:                                             ; preds = %start
  %2 = getelementptr inbounds nuw i8, ptr %endowment, i64 24
  %_97 = load i64, ptr %2, align 8, !noundef !4
  %3 = getelementptr inbounds nuw i8, ptr %self, i64 72
  %_98 = load i64, ptr %3, align 8, !noundef !4
  %_96.not = icmp ugt i64 %_97, %_98
  br i1 %_96.not, label %bb25, label %bb28

bb28:                                             ; preds = %bb27
  %4 = getelementptr inbounds nuw i8, ptr %endowment, i64 32
  %_99 = load i64, ptr %4, align 8, !noundef !4
  %5 = getelementptr inbounds nuw i8, ptr %self, i64 80
  %_100 = load i64, ptr %5, align 8, !noundef !4
  %_4.not = icmp ugt i64 %_99, %_100
  br i1 %_4.not, label %bb25, label %bb1

bb1:                                              ; preds = %bb28
  tail call void @llvm.experimental.noalias.scope.decl(metadata !450)
  %6 = getelementptr inbounds nuw i8, ptr %self, i64 52
  %_4.i = load i32, ptr %6, align 4, !alias.scope !450, !noundef !4
  tail call void @llvm.experimental.noalias.scope.decl(metadata !453)
  %_0.i.i.i8.i.not.i = icmp eq i32 %_4.i, 0
  br i1 %_0.i.i.i8.i.not.i, label %bb25, label %bb3.lr.ph.i.i

bb3.lr.ph.i.i:                                    ; preds = %bb1
  %7 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_11.i.i.i.i = load ptr, ptr %7, align 8, !alias.scope !456, !noalias !457, !nonnull !4, !noundef !4
  %8 = getelementptr inbounds nuw i8, ptr %self, i64 16
  %_10.i.i.i.i = load i64, ptr %8, align 8, !alias.scope !456, !noalias !457, !noundef !4
  %_14.idx.i.i.i.i = mul nuw nsw i64 %_10.i.i.i.i, 24
  %_14.i.i.i.i = getelementptr inbounds nuw i8, ptr %_11.i.i.i.i, i64 %_14.idx.i.i.i.i
  %_6.i8.not.i.i.i.i.i.i = icmp eq i64 %_10.i.i.i.i, 0
  br i1 %_6.i8.not.i.i.i.i.i.i, label %bb33, label %bb3.i.i

bb1.loopexit.i.i:                                 ; preds = %bb5.i.i.i.i.i.i.i
  %exitcond.not.i.i = icmp eq i32 %_0.i1.i.i.i.i, %_4.i
  br i1 %exitcond.not.i.i, label %bb25, label %bb3.i.i

bb3.i.i:                                          ; preds = %bb1.loopexit.i.i, %bb3.lr.ph.i.i
  %_0.i1.i.i79.i.i = phi i32 [ %_0.i1.i.i.i.i, %bb1.loopexit.i.i ], [ 0, %bb3.lr.ph.i.i ]
  %_0.i1.i.i.i.i = add nuw i32 %_0.i1.i.i79.i.i, 1
  br label %bb3.i.i.i.i.i.i

bb3.i.i.i.i.i.i:                                  ; preds = %bb1.backedge.i.i.i.i.i.i, %bb3.i.i
  %_13.i79.i.i.i.i.i.i = phi ptr [ %_13.i.i.i.i.i.i.i, %bb1.backedge.i.i.i.i.i.i ], [ %_11.i.i.i.i, %bb3.i.i ]
  %_13.i.i.i.i.i.i.i = getelementptr inbounds nuw i8, ptr %_13.i79.i.i.i.i.i.i, i64 24
  %_4.i.i.i.i.i.i.i.i = load i64, ptr %_13.i79.i.i.i.i.i.i, align 8, !range !148, !alias.scope !464, !noalias !469, !noundef !4
  %9 = trunc nuw i64 %_4.i.i.i.i.i.i.i.i to i1
  br i1 %9, label %bb5.i.i.i.i.i.i.i, label %bb1.backedge.i.i.i.i.i.i

bb5.i.i.i.i.i.i.i:                                ; preds = %bb3.i.i.i.i.i.i
  %10 = getelementptr inbounds nuw i8, ptr %_13.i79.i.i.i.i.i.i, i64 16
  %c.i.i.i.i.i.i.i.i.i = load i32, ptr %10, align 8, !alias.scope !477, !noalias !482, !noundef !4
  %_0.i.i.i.i.i.i.i.i.i = icmp eq i32 %c.i.i.i.i.i.i.i.i.i, %_0.i1.i.i79.i.i
  br i1 %_0.i.i.i.i.i.i.i.i.i, label %bb1.loopexit.i.i, label %bb1.backedge.i.i.i.i.i.i

bb1.backedge.i.i.i.i.i.i:                         ; preds = %bb5.i.i.i.i.i.i.i, %bb3.i.i.i.i.i.i
  %_6.i.not.i.i.i.i.i.i = icmp eq ptr %_13.i.i.i.i.i.i.i, %_14.i.i.i.i
  br i1 %_6.i.not.i.i.i.i.i.i, label %bb33, label %bb3.i.i.i.i.i.i

bb25:                                             ; preds = %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit, %bb37, %bb35, %bb1.i.i, %bb1.loopexit.i.i, %bb1, %bb28, %bb27, %start
  %_0.sroa.9.0 = phi i64 [ %_0.sroa.9.1, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit ], [ 1280, %bb27 ], [ 1280, %start ], [ 1280, %bb28 ], [ 1536, %bb35 ], [ 1280, %bb1 ], [ %19, %bb37 ], [ 1536, %bb1.i.i ], [ 1280, %bb1.loopexit.i.i ]
  call void @llvm.experimental.noalias.scope.decl(metadata !485)
  call void @llvm.experimental.noalias.scope.decl(metadata !488)
  call void @llvm.experimental.noalias.scope.decl(metadata !491)
  call void @llvm.experimental.noalias.scope.decl(metadata !494)
  call void @llvm.experimental.noalias.scope.decl(metadata !497)
  %_9.i.i.i.i.i = load i64, ptr %program, align 8, !range !123, !alias.scope !500, !noundef !4
  %11 = icmp eq i64 %_9.i.i.i.i.i, 0
  br i1 %11, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit, label %bb6.i.i.i.i.i

bb6.i.i.i.i.i:                                    ; preds = %bb25
  %12 = getelementptr inbounds nuw i8, ptr %program, i64 8
  %_19.i.i.i.i.i = load ptr, ptr %12, align 8, !alias.scope !500, !nonnull !4, !noundef !4
  call void @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_dealloc(ptr noundef nonnull %_19.i.i.i.i.i, i64 noundef range(i64 1, 0) %_9.i.i.i.i.i, i64 noundef range(i64 1, 5) 1) #22, !noalias !500
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit: ; preds = %bb6.i.i.i.i.i, %bb25
  %13 = and i64 %_0.sroa.9.0, 65280
  br label %bb26

bb33:                                             ; preds = %bb1.backedge.i.i.i.i.i.i, %bb3.lr.ph.i.i
  %.ph = phi i32 [ 0, %bb3.lr.ph.i.i ], [ %_0.i1.i.i79.i.i, %bb1.backedge.i.i.i.i.i.i ]
  %n.i = tail call noundef i32 @__vm_cap_count() #22
  %smax.i.i = tail call i32 @llvm.smax.i32(i32 %n.i, i32 0)
  br label %bb1.i.i

bb1.i.i:                                          ; preds = %bb3.i.i22, %bb33
  %14 = phi i32 [ %15, %bb3.i.i22 ], [ 0, %bb33 ]
  %exitcond.not.i.not.not.not.not.i.not = icmp eq i32 %14, %smax.i.i
  br i1 %exitcond.not.i.not.not.not.not.i.not, label %bb25, label %bb3.i.i22

bb3.i.i22:                                        ; preds = %bb1.i.i
  %15 = add nuw i32 %14, 1
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %ty.i.i.i.i), !noalias !501
  store i32 -1, ptr %ty.i.i.i.i, align 4, !noalias !501
  %h.i.i.i.i = call noundef i32 @__vm_cap_at(i32 noundef %14, ptr noundef nonnull %ty.i.i.i.i) #22, !noalias !501
  %_7.i.i.i.i = load i32, ptr %ty.i.i.i.i, align 4, !noalias !501, !noundef !4
  %_6.i.i.i.i = icmp eq i32 %_7.i.i.i.i, 6
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %ty.i.i.i.i), !noalias !501
  br i1 %_6.i.i.i.i, label %bb35, label %bb1.i.i

bb35:                                             ; preds = %bb3.i.i22
  %h.i = call noundef i32 @__vm_cap_resolve(ptr noundef nonnull @alloc_cba4392de44c79dd503043d101897357, i64 noundef 10) #22
  %_6.i = icmp sgt i32 %h.i, -1
  br i1 %_6.i, label %bb37, label %bb25

bb37:                                             ; preds = %bb35
  %16 = getelementptr inbounds nuw i8, ptr %self, i64 24
  %_118 = load i64, ptr %16, align 8, !noundef !4
  %_120 = zext i32 %.ph to i64
  %_119 = shl nuw nsw i64 %_120, 16
  %_23 = add i64 %_118, %_119
  %17 = getelementptr inbounds nuw i8, ptr %program, i64 8
  %_125 = load ptr, ptr %17, align 8, !nonnull !4, !noundef !4
  %18 = getelementptr inbounds nuw i8, ptr %program, i64 16
  %_124 = load i64, ptr %18, align 8, !noundef !4
  %19 = call fastcc i64 @_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat11args_region(i64 noundef %_23, ptr noalias noundef readonly align 8 captures(none) dereferenceable(24) %0, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %_125, i64 noundef %_124) #22
  %.sroa.6.0.extract.shift = lshr i64 %19, 32
  %.sroa.6.0.extract.trunc = trunc nuw nsw i64 %.sroa.6.0.extract.shift to i32
  %20 = trunc i64 %19 to i1
  br i1 %20, label %bb25, label %bb39

bb39:                                             ; preds = %bb37
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %grants)
  %21 = getelementptr inbounds nuw i8, ptr %endowment, i64 8
  %_91.1 = load i64, ptr %21, align 8, !noundef !4
  %_29 = add i64 %_91.1, 1
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %_133)
  call fastcc void @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner15try_allocate_inCsIEB7taFyf8_10unir_temen(ptr noalias noundef sret([24 x i8]) align 8 captures(none) dereferenceable(24) %_133, i64 noundef %_29, i1 noundef zeroext false, i64 noundef 4, i64 noundef 16) #22
  %_134 = load i64, ptr %_133, align 8, !range !148, !noundef !4
  %22 = trunc nuw i64 %_134 to i1
  %23 = getelementptr inbounds nuw i8, ptr %_133, i64 8
  %_137.0 = load i64, ptr %23, align 8, !range !441, !noundef !4
  %24 = getelementptr inbounds nuw i8, ptr %_133, i64 16
  br i1 %22, label %bb41, label %bb42, !prof !147

bb41:                                             ; preds = %bb39
  %_137.1 = load i64, ptr %24, align 8
  call void @_RNvNtCsksNX8Mxey3D_5alloc7raw_vec12handle_error(i64 noundef %_137.0, i64 %_137.1) #28
  unreachable

bb42:                                             ; preds = %bb39
  %_132.1 = load ptr, ptr %24, align 8, !nonnull !4, !noundef !4
  %_136 = icmp ule i64 %_29, %_137.0
  call void @llvm.assume(i1 %_136)
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %_133)
  store i64 %_137.0, ptr %grants, align 8
  %25 = getelementptr inbounds nuw i8, ptr %grants, i64 8
  store ptr %_132.1, ptr %25, align 8
  %26 = getelementptr inbounds nuw i8, ptr %grants, i64 16
  store i64 0, ptr %26, align 8
  %_32.0 = load ptr, ptr %endowment, align 8, !nonnull !4, !align !6, !noundef !4
  %_145.idx = mul nuw nsw i64 %_91.1, 24
  %_145 = getelementptr inbounds nuw i8, ptr %_32.0, i64 %_145.idx
  %_15147 = icmp eq i64 %_91.1, 0
  br i1 %_15147, label %bb43, label %bb44

bb44:                                             ; preds = %bb50, %bb42
  %_17753 = phi ptr [ %_177, %bb50 ], [ %_132.1, %bb42 ]
  %_169 = phi i64 [ %30, %bb50 ], [ 0, %bb42 ]
  %iter.sroa.0.048 = phi ptr [ %_157, %bb50 ], [ %_32.0, %bb42 ]
  %_157 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.048, i64 24
  %27 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.048, i64 16
  %_38 = load i32, ptr %27, align 8, !noundef !4
  %_159 = icmp slt i32 %_38, 0
  br i1 %_159, label %bb19, label %bb46

bb43.loopexit:                                    ; preds = %bb50
  %_186.pre = load i64, ptr %grants, align 8, !range !123
  br label %bb43

bb43:                                             ; preds = %bb43.loopexit, %bb42
  %_186 = phi i64 [ %_186.pre, %bb43.loopexit ], [ %_137.0, %bb42 ]
  %_179 = phi i64 [ %30, %bb43.loopexit ], [ 0, %bb42 ]
  %_180 = icmp eq i64 %_179, %_186
  br i1 %_180, label %bb51, label %bb54

bb46:                                             ; preds = %bb44
  %_92.0 = load ptr, ptr %iter.sroa.0.048, align 8, !nonnull !4, !align !5, !noundef !4
  %28 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.048, i64 8
  %_92.1 = load i64, ptr %28, align 8, !noundef !4
  %29 = ptrtoint ptr %_92.0 to i64
  %_42 = trunc i64 %29 to i32
  %_44 = trunc i64 %_92.1 to i32
  %_176 = load i64, ptr %grants, align 8, !range !123, !noundef !4
  %_170 = icmp eq i64 %_169, %_176
  br i1 %_170, label %bb47, label %bb50

bb47:                                             ; preds = %bb46
  call void @_RNvMs3_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_E8grow_oneCsIEB7taFyf8_10unir_temen(ptr noalias noundef nonnull align 8 dereferenceable(16) %grants) #31
  %_177.pre = load ptr, ptr %25, align 8
  br label %bb50

bb50:                                             ; preds = %bb47, %bb46
  %_177 = phi ptr [ %_17753, %bb46 ], [ %_177.pre, %bb47 ]
  %_174 = getelementptr inbounds nuw [4 x i32], ptr %_177, i64 %_169
  store i32 %_42, ptr %_174, align 4
  %_41.sroa.4.0._174.sroa_idx = getelementptr inbounds nuw i8, ptr %_174, i64 4
  store i32 %_44, ptr %_41.sroa.4.0._174.sroa_idx, align 4
  %_41.sroa.5.0._174.sroa_idx = getelementptr inbounds nuw i8, ptr %_174, i64 8
  store i32 %_38, ptr %_41.sroa.5.0._174.sroa_idx, align 4
  %_41.sroa.6.0._174.sroa_idx = getelementptr inbounds nuw i8, ptr %_174, i64 12
  store i32 0, ptr %_41.sroa.6.0._174.sroa_idx, align 4
  %30 = add i64 %_169, 1
  store i64 %30, ptr %26, align 8
  %_151 = icmp eq ptr %_157, %_145
  br i1 %_151, label %bb43.loopexit, label %bb44

bb19:                                             ; preds = %bb11, %bb44
  %_0.sroa.9.1 = phi i64 [ 1536, %bb11 ], [ 256, %bb44 ]
  call void @llvm.experimental.noalias.scope.decl(metadata !504)
  call void @llvm.experimental.noalias.scope.decl(metadata !507)
  call void @llvm.experimental.noalias.scope.decl(metadata !510)
  call void @llvm.experimental.noalias.scope.decl(metadata !513)
  %_9.i.i.i.i = load i64, ptr %grants, align 8, !range !123, !alias.scope !516, !noundef !4
  %31 = icmp eq i64 %_9.i.i.i.i, 0
  br i1 %31, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit, label %bb6.i.i.i.i

bb6.i.i.i.i:                                      ; preds = %bb19
  %_10.i.i.i.i23 = shl nuw i64 %_9.i.i.i.i, 4
  %_19.i.i.i.i = load ptr, ptr %25, align 8, !alias.scope !516, !nonnull !4, !noundef !4
  call void @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_dealloc(ptr noundef nonnull %_19.i.i.i.i, i64 noundef range(i64 1, 0) %_10.i.i.i.i23, i64 noundef range(i64 1, 5) 4) #22, !noalias !516
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit: ; preds = %bb6.i.i.i.i, %bb19
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %grants)
  br label %bb25

bb51:                                             ; preds = %bb43
  call void @_RNvMs3_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_E8grow_oneCsIEB7taFyf8_10unir_temen(ptr noalias noundef nonnull align 8 dereferenceable(16) %grants) #31
  br label %bb54

bb54:                                             ; preds = %bb51, %bb43
  %_187 = load ptr, ptr %25, align 8, !nonnull !4, !noundef !4
  %_184 = getelementptr inbounds nuw [4 x i32], ptr %_187, i64 %_179
  store i32 ptrtoint (ptr @alloc_ddb4cde4dbe5ec370c8c8cf52e622a70 to i32), ptr %_184, align 4
  %_47.sroa.4.0._184.sroa_idx = getelementptr inbounds nuw i8, ptr %_184, i64 4
  store i32 9, ptr %_47.sroa.4.0._184.sroa_idx, align 4
  %_47.sroa.5.0._184.sroa_idx = getelementptr inbounds nuw i8, ptr %_184, i64 8
  store i32 %.sroa.6.0.extract.trunc, ptr %_47.sroa.5.0._184.sroa_idx, align 4
  %_47.sroa.6.0._184.sroa_idx = getelementptr inbounds nuw i8, ptr %_184, i64 12
  store i32 0, ptr %_47.sroa.6.0._184.sroa_idx, align 4
  %32 = add i64 %_179, 1
  %33 = getelementptr inbounds nuw i8, ptr %self, i64 40
  %_55 = load i64, ptr %33, align 8, !noundef !4
  %34 = getelementptr inbounds nuw i8, ptr %self, i64 48
  %_58 = load i32, ptr %34, align 8, !noundef !4
  %35 = and i32 %_58, 63
  %36 = zext nneg i32 %35 to i64
  %_56 = shl i64 %_120, %36
  %off = add i64 %_56, %_55
  %_7.i = icmp eq i64 %_94, 0
  %.self.i = call i64 @llvm.umin.i64(i64 %_94, i64 9223372036854775807)
  %_0.sroa.0.0.i24 = select i1 %_7.i, i64 1, i64 %.self.i
  %_63 = zext nneg i32 %h.i to i64
  %_64 = ptrtoint ptr %_187 to i64
  %_189 = icmp ult i64 %32, 576460752303423488
  call void @llvm.assume(i1 %_189)
  %_69 = zext i32 %_58 to i64
  %h4 = call noundef i64 @__vm_instantiate(i32 noundef %h.i.i.i.i, i64 noundef %_63, i64 noundef %_64, i64 noundef %32, i64 noundef 0, i64 noundef %off, i64 noundef %_69, i64 noundef %_0.sroa.0.0.i24) #22
  %_71 = icmp slt i64 %h4, 0
  br i1 %_71, label %bb11, label %bb13

bb13:                                             ; preds = %bb54
  %37 = sub i64 %_95, %_94
  store i64 %37, ptr %1, align 8
  %38 = sub i64 %_98, %_97
  store i64 %38, ptr %3, align 8
  %39 = sub i64 %_100, %_99
  store i64 %39, ptr %5, align 8
  %_195 = load ptr, ptr %7, align 8, !nonnull !4, !noundef !4
  %_194 = load i64, ptr %8, align 8, !noundef !4
  %_198.idx = mul nuw nsw i64 %_194, 24
  %_198 = getelementptr inbounds nuw i8, ptr %_195, i64 %_198.idx
  %_295.i = icmp eq i64 %_194, 0
  br i1 %_295.i, label %bb15, label %bb21.i

bb21.i:                                           ; preds = %bb7.i, %bb13
  %i.sroa.0.07.i = phi i64 [ %40, %bb7.i ], [ 0, %bb13 ]
  %_3646.i = phi ptr [ %_36.i, %bb7.i ], [ %_195, %bb13 ]
  %_3.i.i.i = load i64, ptr %_3646.i, align 8, !range !148, !alias.scope !517, !noalias !522, !noundef !4
  %_2.not.i.i.i = icmp eq i64 %_3.i.i.i, 0
  br i1 %_2.not.i.i.i, label %bb16, label %bb7.i

bb7.i:                                            ; preds = %bb21.i
  %_36.i = getelementptr inbounds nuw i8, ptr %_3646.i, i64 24
  %40 = add nuw nsw i64 %i.sroa.0.07.i, 1
  %_29.i = icmp eq ptr %_36.i, %_198
  br i1 %_29.i, label %bb15, label %bb21.i

bb11:                                             ; preds = %bb54
  %_4.i28 = call noundef i64 @__vm_region_unmap(i32 noundef %.sroa.6.0.extract.trunc, i64 noundef %_23, i64 noundef 65536) #22
  br label %bb19

bb16:                                             ; preds = %bb21.i
  %_14.i = icmp ult i64 %i.sroa.0.07.i, %_194
  call void @llvm.assume(i1 %_14.i)
  br label %bb17

bb15:                                             ; preds = %bb7.i, %bb13
  %_209 = load i64, ptr %self, align 8, !range !123, !noundef !4
  %_203 = icmp eq i64 %_194, %_209
  br i1 %_203, label %bb55, label %bb58

bb55:                                             ; preds = %bb15
  call void @_RNvMs3_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEE8grow_oneCsIEB7taFyf8_10unir_temen(ptr noalias noundef nonnull align 8 dereferenceable(16) %self) #31
  %_210.pre = load ptr, ptr %7, align 8
  br label %bb58

bb58:                                             ; preds = %bb55, %bb15
  %_210 = phi ptr [ %_195, %bb15 ], [ %_210.pre, %bb55 ]
  %_207 = getelementptr inbounds nuw %"core::option::Option<(i64, u32, i32)>", ptr %_210, i64 %_194
  store i64 0, ptr %_207, align 8
  %41 = add nsw i64 %_194, 1
  store i64 %41, ptr %8, align 8
  br label %bb17

bb17:                                             ; preds = %bb58, %bb16
  %_216 = phi ptr [ %_195, %bb16 ], [ %_210, %bb58 ]
  %_215 = phi i64 [ %_194, %bb16 ], [ %41, %bb58 ]
  %i.sroa.0.0 = phi i64 [ %i.sroa.0.07.i, %bb16 ], [ %_194, %bb58 ]
  %_217 = icmp ult i64 %i.sroa.0.0, %_215
  br i1 %_217, label %bb59, label %panic

bb59:                                             ; preds = %bb17
  %_87 = getelementptr inbounds nuw %"core::option::Option<(i64, u32, i32)>", ptr %_216, i64 %i.sroa.0.0
  store i64 1, ptr %_87, align 8
  %_85.sroa.4.0._87.sroa_idx = getelementptr inbounds nuw i8, ptr %_87, i64 8
  store i64 %h4, ptr %_85.sroa.4.0._87.sroa_idx, align 8
  %_85.sroa.4.sroa.4.0._85.sroa.4.0._87.sroa_idx.sroa_idx = getelementptr inbounds nuw i8, ptr %_87, i64 16
  store i32 %.ph, ptr %_85.sroa.4.sroa.4.0._85.sroa.4.0._87.sroa_idx.sroa_idx, align 8
  %_85.sroa.4.sroa.5.0._85.sroa.4.0._87.sroa_idx.sroa_idx = getelementptr inbounds nuw i8, ptr %_87, i64 20
  store i32 %.sroa.6.0.extract.trunc, ptr %_85.sroa.4.sroa.5.0._85.sroa.4.0._87.sroa_idx.sroa_idx, align 4
  call void @llvm.experimental.noalias.scope.decl(metadata !525)
  call void @llvm.experimental.noalias.scope.decl(metadata !528)
  call void @llvm.experimental.noalias.scope.decl(metadata !531)
  call void @llvm.experimental.noalias.scope.decl(metadata !534)
  %_9.i.i.i.i29 = load i64, ptr %grants, align 8, !range !123, !alias.scope !537, !noundef !4
  %42 = icmp eq i64 %_9.i.i.i.i29, 0
  br i1 %42, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit33, label %bb6.i.i.i.i30

bb6.i.i.i.i30:                                    ; preds = %bb59
  %_10.i.i.i.i31 = shl nuw i64 %_9.i.i.i.i29, 4
  call void @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_dealloc(ptr noundef nonnull %_187, i64 noundef range(i64 1, 0) %_10.i.i.i.i31, i64 noundef range(i64 1, 5) 4) #22, !noalias !537
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit33

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit33: ; preds = %bb6.i.i.i.i30, %bb59
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %grants)
  call void @llvm.experimental.noalias.scope.decl(metadata !538)
  call void @llvm.experimental.noalias.scope.decl(metadata !541)
  call void @llvm.experimental.noalias.scope.decl(metadata !544)
  call void @llvm.experimental.noalias.scope.decl(metadata !547)
  call void @llvm.experimental.noalias.scope.decl(metadata !550)
  %_9.i.i.i.i.i34 = load i64, ptr %program, align 8, !range !123, !alias.scope !553, !noundef !4
  %43 = icmp eq i64 %_9.i.i.i.i.i34, 0
  br i1 %43, label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37, label %bb6.i.i.i.i.i35

bb6.i.i.i.i.i35:                                  ; preds = %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit33
  call void @_RNvCs1Y7DaGC1cwg_7___rustc14___rust_dealloc(ptr noundef nonnull %_125, i64 noundef range(i64 1, 0) %_9.i.i.i.i.i34, i64 noundef range(i64 1, 5) 1) #22, !noalias !553
  br label %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37

_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37: ; preds = %bb6.i.i.i.i.i35, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen.exit33
  %_89 = shl i64 %i.sroa.0.0, 32
  %44 = or i64 %_89, -9223372036854775808
  br label %bb26

panic:                                            ; preds = %bb17
  call void @_RNvNtCscliFh4jUES5_4core9panicking18panic_bounds_check(i64 noundef %i.sroa.0.0, i64 noundef %_215, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #29
  unreachable

bb26:                                             ; preds = %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit
  %_0.sroa.0.2 = phi i64 [ 1, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit ], [ 0, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37 ]
  %_0.sroa.9.0.insert.insert = phi i64 [ %13, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit ], [ %44, %_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_.exit37 ]
  %_0.sroa.0.0.insert.insert = or i64 %_0.sroa.9.0.insert.insert, %_0.sroa.0.2
  ret i64 %_0.sroa.0.0.insert.insert
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc range(i64 512, 9223372032559810560) i64 @_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat11args_region(i64 noundef %at, ptr dead_on_return noalias noundef nonnull readonly align 8 captures(none) dereferenceable(24) %budgets, ptr noalias noundef nonnull readonly align 1 captures(address) %args.0, i64 noundef range(i64 0, -9223372036854775808) %args.1) unnamed_addr #1 {
start:
  %r = alloca [16 x i8], align 8
  %_5 = icmp samesign ugt i64 %args.1, 65504
  br i1 %_5, label %bb12, label %bb2

bb2:                                              ; preds = %start
  %h = tail call noundef i64 @__vm_region_create(i64 noundef 65536) #22
  %0 = add i64 %h, -2147483648
  %or.cond = icmp ult i64 %0, -4294967296
  br i1 %or.cond, label %bb12, label %bb15

bb15:                                             ; preds = %bb2
  %_33 = trunc nsw i64 %h to i32
  %_14 = icmp slt i64 %h, 0
  br i1 %_14, label %bb12, label %bb6

bb6:                                              ; preds = %bb15
  %r.i = tail call noundef i64 @__vm_region_map(i32 noundef %_33, i64 noundef %at, i64 noundef 0, i64 noundef 65536, i32 noundef 3) #22
  %_7.i = icmp sgt i64 %r.i, -1
  br i1 %_7.i, label %bb30, label %bb12

bb30:                                             ; preds = %bb6
  call void @llvm.lifetime.start.p0(i64 16, ptr nonnull %r)
  store i64 %at, ptr %r, align 8
  %1 = getelementptr inbounds nuw i8, ptr %r, i64 8
  store i64 65536, ptr %1, align 8
  %_24 = trunc nuw nsw i64 %args.1 to i32
  %_8.i = inttoptr i64 %at to ptr
  store atomic i32 %_24, ptr %_8.i seq_cst, align 4, !noalias !554
  %_44 = load i64, ptr %budgets, align 8, !noundef !4
  %_13.i.i = add i64 %at, 8
  %_8.i22 = inttoptr i64 %_13.i.i to ptr
  store atomic i64 %_44, ptr %_8.i22 seq_cst, align 8, !noalias !557
  %2 = getelementptr inbounds nuw i8, ptr %budgets, i64 8
  %_47 = load i64, ptr %2, align 8, !noundef !4
  %_13.i.i28 = add i64 %at, 16
  %_8.i29 = inttoptr i64 %_13.i.i28 to ptr
  store atomic i64 %_47, ptr %_8.i29 seq_cst, align 8, !noalias !560
  %3 = getelementptr inbounds nuw i8, ptr %budgets, i64 16
  %_50 = load i64, ptr %3, align 8, !noundef !4
  %_13.i.i36 = add i64 %at, 24
  %_8.i37 = inttoptr i64 %_13.i.i36 to ptr
  store atomic i64 %_50, ptr %_8.i37 seq_cst, align 8, !noalias !563
  %4 = call noundef i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef nonnull readonly align 8 captures(address, read_provenance) dereferenceable(16) %r, i64 noundef 32, ptr noalias noundef nonnull readonly align 1 captures(address, read_provenance) %args.0, i64 noundef %args.1) #22
  %.not17 = icmp eq i8 %4, 7
  br i1 %.not17, label %bb31, label %bb9

bb9:                                              ; preds = %bb30
  %_4.i40 = tail call noundef i64 @__vm_region_unmap(i32 noundef %_33, i64 noundef %at, i64 noundef 65536) #22
  br label %bb31

bb31:                                             ; preds = %bb9, %bb30
  %_0.sroa.0.0 = phi i64 [ 1, %bb9 ], [ 0, %bb30 ]
  call void @llvm.lifetime.end.p0(i64 16, ptr nonnull %r)
  %5 = shl nuw nsw i64 %h, 32
  %6 = zext nneg i8 %4 to i64
  %7 = shl nuw nsw i64 %6, 8
  br label %bb12

bb12:                                             ; preds = %bb31, %bb6, %bb15, %bb2, %start
  %_0.sroa.123.1 = phi i64 [ %5, %bb31 ], [ 0, %start ], [ 0, %bb2 ], [ 0, %bb15 ], [ 0, %bb6 ]
  %_0.sroa.7.1 = phi i64 [ %7, %bb31 ], [ 512, %start ], [ 1024, %bb2 ], [ 1024, %bb15 ], [ 512, %bb6 ]
  %_0.sroa.0.1 = phi i64 [ %_0.sroa.0.0, %bb31 ], [ 1, %start ], [ 1, %bb2 ], [ 1, %bb15 ], [ 1, %bb6 ]
  %_0.sroa.7.0.insert.insert = or disjoint i64 %_0.sroa.7.1, %_0.sroa.123.1
  %_0.sroa.0.0.insert.insert = or i64 %_0.sroa.7.0.insert.insert, %_0.sroa.0.1
  ret i64 %_0.sroa.0.0.insert.insert
}

; Function Attrs: nounwind nonlazybind uwtable
define internal fastcc void @_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner15try_allocate_inCsIEB7taFyf8_10unir_temen(ptr dead_on_unwind noalias noundef nonnull writable writeonly sret([24 x i8]) align 8 captures(none) dereferenceable(24) initializes((0, 16)) %_0, i64 noundef %capacity, i1 noundef zeroext %init, i64 noundef range(i64 1, 5) %elem_layout.0, i64 noundef range(i64 1, 17) %elem_layout.1) unnamed_addr #1 {
start:
  %_7.i = add nsw i64 %elem_layout.0, -1
  %_9.i = add nuw nsw i64 %_7.i, %elem_layout.1
  %_11.i = sub nsw i64 0, %elem_layout.0
  %padded.i = and i64 %_9.i, %_11.i
  %0 = tail call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %padded.i, i64 %capacity)
  %_17.0.i = extractvalue { i64, i1 } %0, 0
  %_17.1.i = extractvalue { i64, i1 } %0, 1
  %_23.i = sub nuw i64 -9223372036854775808, %elem_layout.0
  %_22.i = icmp ugt i64 %_17.0.i, %_23.i
  %or.cond.i = select i1 %_17.1.i, i1 true, i1 %_22.i
  br i1 %or.cond.i, label %bb13, label %bb14, !prof !431

bb13:                                             ; preds = %start
  %1 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 0, ptr %1, align 8
  br label %bb11

bb14:                                             ; preds = %start
  %2 = icmp eq i64 %_17.0.i, 0
  br i1 %2, label %bb2, label %bb3

bb2:                                              ; preds = %bb14
  %_32 = inttoptr i64 %elem_layout.0 to ptr
  %3 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 0, ptr %3, align 8
  %4 = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store ptr %_32, ptr %4, align 8
  br label %bb11

bb3:                                              ; preds = %bb14
  tail call void @_RNvCs1Y7DaGC1cwg_7___rustc35___rust_no_alloc_shim_is_unstable_v2() #22
  br i1 %init, label %bb4, label %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator8allocate.exit

bb11:                                             ; preds = %bb10, %bb9, %bb2, %bb13
  %.sink = phi i64 [ 1, %bb9 ], [ 1, %bb13 ], [ 0, %bb10 ], [ 0, %bb2 ]
  store i64 %.sink, ptr %_0, align 8
  ret void

bb4:                                              ; preds = %bb3
  %5 = tail call noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc19___rust_alloc_zeroed(i64 noundef range(i64 1, 0) %_17.0.i, i64 noundef range(i64 1, -9223372036854775807) %elem_layout.0) #22
  br label %bb8

_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator8allocate.exit: ; preds = %bb3
  %6 = tail call noundef ptr @_RNvCs1Y7DaGC1cwg_7___rustc12___rust_alloc(i64 noundef %_17.0.i, i64 noundef range(i64 1, -9223372036854775807) %elem_layout.0) #22
  br label %bb8

bb8:                                              ; preds = %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator8allocate.exit, %bb4
  %.pn6 = phi ptr [ %5, %bb4 ], [ %6, %_RNvXs_NtCsksNX8Mxey3D_5alloc5allocNtB4_6GlobalNtNtCscliFh4jUES5_4core5alloc9Allocator8allocate.exit ]
  %7 = icmp eq ptr %.pn6, null
  br i1 %7, label %bb9, label %bb10

bb9:                                              ; preds = %bb8
  %8 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %elem_layout.0, ptr %8, align 8
  %9 = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store i64 %_17.0.i, ptr %9, align 8
  br label %bb11

bb10:                                             ; preds = %bb8
  %10 = icmp sgt i64 %capacity, -1
  tail call void @llvm.assume(i1 %10)
  %11 = getelementptr inbounds nuw i8, ptr %_0, i64 8
  store i64 %capacity, ptr %11, align 8
  %12 = getelementptr inbounds nuw i8, ptr %_0, i64 16
  store ptr %.pn6, ptr %12, align 8
  br label %bb11
}

; Function Attrs: nounwind nonlazybind uwtable
declare noundef i64 @__vm_instantiate(i32 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef) unnamed_addr #1

; Function Attrs: nounwind nonlazybind uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region5write(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, ptr noalias noundef nonnull readonly align 1 captures(address) %src.0, i64 noundef range(i64 0, -9223372036854775808) %src.1) unnamed_addr #1 {
start:
  %_54.0 = add i64 %src.1, %off
  %_54.1 = icmp ult i64 %_54.0, %off
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 8
  %_50 = load i64, ptr %0, align 8
  %_49 = icmp ugt i64 %_54.0, %_50
  %or.cond17 = select i1 %_54.1, i1 true, i1 %_49
  br i1 %or.cond17, label %bb6, label %bb12, !prof !431

bb12:                                             ; preds = %start
  %_52 = load i64, ptr %self, align 8, !noundef !4
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
  %b = load i8, ptr %iter.sroa.0.020, align 1, !noundef !4
  %_19 = add i64 %iter.sroa.5.019, %_51
  %_18 = inttoptr i64 %_19 to ptr
  store volatile i8 %b, ptr %_18, align 1
  %_90.1 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 2
  %_79.0.1 = or disjoint i64 %iter.sroa.5.019, 2
  %b.1 = load i8, ptr %_90, align 1, !noundef !4
  %_19.1 = add i64 %_79.0, %_51
  %_18.1 = inttoptr i64 %_19.1 to ptr
  store volatile i8 %b.1, ptr %_18.1, align 1
  %_90.2 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 3
  %_79.0.2 = or disjoint i64 %iter.sroa.5.019, 3
  %b.2 = load i8, ptr %_90.1, align 1, !noundef !4
  %_19.2 = add i64 %_79.0.1, %_51
  %_18.2 = inttoptr i64 %_19.2 to ptr
  store volatile i8 %b.2, ptr %_18.2, align 1
  %_90.3 = getelementptr inbounds nuw i8, ptr %iter.sroa.0.020, i64 4
  %_79.0.3 = add nuw nsw i64 %iter.sroa.5.019, 4
  %b.3 = load i8, ptr %_90.2, align 1, !noundef !4
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
  %b.epil = load i8, ptr %iter.sroa.0.020.epil, align 1, !noundef !4
  %_19.epil = add i64 %iter.sroa.5.019.epil, %_51
  %_18.epil = inttoptr i64 %_19.epil to ptr
  store volatile i8 %b.epil, ptr %_18.epil, align 1
  %epil.iter.next = add i64 %epil.iter, 1
  %epil.iter.cmp.not = icmp eq i64 %epil.iter.next, %xtraiter
  br i1 %epil.iter.cmp.not, label %bb17, label %bb18.epil, !llvm.loop !566

bb17:                                             ; preds = %bb18.epil, %bb17.loopexit.unr-lcssa, %bb12
  %t = add i64 %words4.i, %spec.store.select.i
  %_97 = icmp ult i64 %t, %spec.store.select.i
  %_93.not = icmp ugt i64 %t, %src.1
  %or.cond = or i1 %_97, %_93.not
  br i1 %or.cond, label %bb21, label %bb20, !prof !434

bb21:                                             ; preds = %bb17
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %spec.store.select.i, i64 noundef %t, i64 noundef %src.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #29
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
  br i1 %epil.iter29.cmp.not, label %bb26, label %bb27.epil, !llvm.loop !567

bb26:                                             ; preds = %bb27.epil, %bb26.loopexit.unr-lcssa, %bb20
  %_36 = add nuw i64 %t, %_9.i
  %_121.not = icmp ugt i64 %_36, %src.1
  br i1 %_121.not, label %bb30, label %bb29, !prof !434

bb30:                                             ; preds = %bb26
  tail call void @_RNvNtNtCscliFh4jUES5_4core5slice5index16slice_index_fail(i64 noundef %t, i64 noundef %_36, i64 noundef %src.1, ptr noalias noundef readonly align 8 captures(address, read_provenance) dereferenceable(24) @alloc_11e2992a5b9c8f277d4a00890b54e14e.83) #29
  unreachable

bb29:                                             ; preds = %bb26
  %_128 = getelementptr inbounds nuw i8, ptr %src.0, i64 %t
  %_14725 = icmp samesign eq i64 %_9.i, 0
  br i1 %_14725, label %bb6, label %bb36.lr.ph

bb36.lr.ph:                                       ; preds = %bb29
  %_42 = add i64 %t, %_51
  %b6 = load i8, ptr %_128, align 1, !noundef !4
  %_40 = inttoptr i64 %_42 to ptr
  store volatile i8 %b6, ptr %_40, align 1
  %_147 = icmp samesign eq i64 %_9.i, 1
  br i1 %_147, label %bb6, label %bb36.1

bb36.1:                                           ; preds = %bb36.lr.ph
  %_153 = getelementptr inbounds nuw i8, ptr %_128, i64 1
  %b6.1 = load i8, ptr %_153, align 1, !noundef !4
  %_41.1 = add i64 %_42, 1
  %_40.1 = inttoptr i64 %_41.1 to ptr
  store volatile i8 %b6.1, ptr %_40.1, align 1
  %_147.1 = icmp samesign eq i64 %_9.i, 2
  br i1 %_147.1, label %bb6, label %bb36.2

bb36.2:                                           ; preds = %bb36.1
  %_153.1 = getelementptr inbounds nuw i8, ptr %_128, i64 2
  %b6.2 = load i8, ptr %_153.1, align 1, !noundef !4
  %_41.2 = add i64 %_42, 2
  %_40.2 = inttoptr i64 %_41.2 to ptr
  store volatile i8 %b6.2, ptr %_40.2, align 1
  %_147.2 = icmp samesign eq i64 %_9.i, 3
  br i1 %_147.2, label %bb6, label %bb36.3

bb36.3:                                           ; preds = %bb36.2
  %_153.2 = getelementptr inbounds nuw i8, ptr %_128, i64 3
  %b6.3 = load i8, ptr %_153.2, align 1, !noundef !4
  %_41.3 = add i64 %_42, 3
  %_40.3 = inttoptr i64 %_41.3 to ptr
  store volatile i8 %b6.3, ptr %_40.3, align 1
  %_147.3 = icmp samesign eq i64 %_9.i, 4
  br i1 %_147.3, label %bb6, label %bb36.4

bb36.4:                                           ; preds = %bb36.3
  %_153.3 = getelementptr inbounds nuw i8, ptr %_128, i64 4
  %b6.4 = load i8, ptr %_153.3, align 1, !noundef !4
  %_41.4 = add i64 %_42, 4
  %_40.4 = inttoptr i64 %_41.4 to ptr
  store volatile i8 %b6.4, ptr %_40.4, align 1
  %_147.4 = icmp samesign eq i64 %_9.i, 5
  br i1 %_147.4, label %bb6, label %bb36.5

bb36.5:                                           ; preds = %bb36.4
  %_153.4 = getelementptr inbounds nuw i8, ptr %_128, i64 5
  %b6.5 = load i8, ptr %_153.4, align 1, !noundef !4
  %_41.5 = add i64 %_42, 5
  %_40.5 = inttoptr i64 %_41.5 to ptr
  store volatile i8 %b6.5, ptr %_40.5, align 1
  %_147.5 = icmp samesign eq i64 %_9.i, 6
  br i1 %_147.5, label %bb6, label %bb36.6

bb36.6:                                           ; preds = %bb36.5
  %_153.5 = getelementptr inbounds nuw i8, ptr %_128, i64 6
  %b6.6 = load i8, ptr %_153.5, align 1, !noundef !4
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
  br i1 %or.cond, label %bb4, label %bb6, !prof !431

bb6:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %region, align 8, !alias.scope !568, !noalias !571, !noundef !4
  %_13.i = add i64 %_14.i, %off
  %spec.store.select = tail call i32 @llvm.umin.i32(i32 %0, i32 2147483647)
  %n = tail call noundef i32 @__vm_notify(i64 noundef %_13.i, i32 noundef %spec.store.select) #22
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
define internal range(i64 0, -4294967294) i64 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u32(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off) unnamed_addr #26 {
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
  br i1 %or.cond, label %bb3, label %bb5, !prof !431

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !573, !noalias !576, !noundef !4
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
define internal void @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region8load_u64(ptr dead_on_unwind noalias noundef writable writeonly sret([16 x i8]) align 8 captures(none) dereferenceable(16) initializes((0, 1)) %_0, ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off) unnamed_addr #26 {
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
  br i1 %or.cond, label %bb4, label %bb5, !prof !431

bb4:                                              ; preds = %bb1.i, %start
  %2 = getelementptr inbounds nuw i8, ptr %_0, i64 1
  store i8 2, ptr %2, align 1
  br label %bb3

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !578, !noalias !581, !noundef !4
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
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, i32 noundef %v) unnamed_addr #26 {
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
  br i1 %or.cond, label %bb3, label %bb5, !prof !431

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !583, !noalias !586, !noundef !4
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  store atomic i32 %v, ptr %_8 seq_cst, align 4
  br label %bb3

bb3:                                              ; preds = %bb5, %bb1.i, %start
  %_0.sroa.0.0 = phi i8 [ 7, %bb5 ], [ 2, %start ], [ 2, %bb1.i ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable
define internal noundef range(i8 2, 8) i8 @_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64(ptr noalias noundef readonly align 8 captures(none) dereferenceable(16) %self, i64 noundef %off, i64 noundef %v) unnamed_addr #26 {
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
  br i1 %or.cond, label %bb3, label %bb5, !prof !431

bb5:                                              ; preds = %bb1.i
  %_14.i = load i64, ptr %self, align 8, !alias.scope !588, !noalias !591, !noundef !4
  %_13.i = add i64 %_14.i, %off
  %_8 = inttoptr i64 %_13.i to ptr
  store atomic i64 %v, ptr %_8 seq_cst, align 8
  br label %bb3

bb3:                                              ; preds = %bb5, %bb1.i, %start
  %_0.sroa.0.0 = phi i8 [ 7, %bb5 ], [ 2, %start ], [ 2, %bb1.i ]
  ret i8 %_0.sroa.0.0
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: readwrite) uwtable
define internal void @_RNvMs0_CsdT7xHiCjqae_9unir_wireNtB5_11FrameHeader6encode(ptr dead_on_unwind noalias noundef writable writeonly sret([9 x i8]) align 1 captures(none) dereferenceable(9) initializes((0, 2)) %_0, ptr noalias noundef readonly align 4 captures(none) dereferenceable(8) %self) unnamed_addr #27 {
start:
  %0 = getelementptr inbounds nuw i8, ptr %self, i64 6
  %_29 = load i16, ptr %0, align 2, !noundef !4
  %1 = icmp ult i16 %_29, 4
  br i1 %1, label %bb3, label %bb13

bb3:                                              ; preds = %start
  %_31 = load i32, ptr %self, align 4, !noundef !4
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
  %_19 = load i16, ptr %4, align 4, !noundef !4
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
define internal { i1, i8 } @_RNvMs4_CsdT7xHiCjqae_9unir_wireNtB5_8Terminal15decode_consumer(i32 noundef %w) unnamed_addr #23 {
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
  %_14.i = load i8, ptr %3, align 1, !range !229, !noundef !4
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
attributes #2 = { cold minsize noreturn nounwind nonlazybind optsize uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #3 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write) }
attributes #4 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #5 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite) }
attributes #6 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #7 = { cold noinline noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #8 = { noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #9 = { nocallback nofree nosync nounwind willreturn memory(argmem: read) }
attributes #10 = { nofree norecurse nosync nounwind nonlazybind memory(argmem: read, inaccessiblemem: write) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #11 = { cold minsize noinline noreturn nounwind nonlazybind optsize uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #12 = { noinline nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #13 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #14 = { nofree norecurse nosync nounwind nonlazybind memory(argmem: readwrite, inaccessiblemem: readwrite) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #15 = { nounwind nonlazybind allockind("alloc,uninitialized,aligned") allocsize(0) uwtable "alloc-family"="__rust_alloc" "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #16 = { nounwind nonlazybind allockind("free") uwtable "alloc-family"="__rust_alloc" "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #17 = { nounwind nonlazybind allockind("realloc,aligned") allocsize(3) uwtable "alloc-family"="__rust_alloc" "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #18 = { cold nofree noreturn nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #19 = { nounwind nonlazybind allockind("alloc,zeroed,aligned") allocsize(0) uwtable "alloc-family"="__rust_alloc" "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #20 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: read) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #21 = { inlinehint nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #22 = { nounwind }
attributes #23 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(none) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #24 = { cold noinline nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #25 = { cold nounwind nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #26 = { mustprogress nofree norecurse nounwind nonlazybind willreturn memory(readwrite, inaccessiblemem: none) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #27 = { mustprogress nofree norecurse nosync nounwind nonlazybind willreturn memory(argmem: readwrite) uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #28 = { noreturn nounwind }
attributes #29 = { noinline noreturn nounwind }
attributes #30 = { inlinehint nounwind }
attributes #31 = { noinline nounwind }

!llvm.ident = !{!0, !0, !0, !0, !0, !0, !0, !0, !0}
!llvm.module.flags = !{!1, !2}

!0 = !{!"rustc version 1.94.1 (e408947bf 2026-03-25)"}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 2, !"RtLibUseGOT", i32 1}
!3 = !{!"branch_weights", !"expected", i32 2000, i32 1}
!4 = !{}
!5 = !{i64 1}
!6 = !{i64 8}
!7 = !{!8, !10, !12}
!8 = distinct !{!8, !9, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!9 = distinct !{!9, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!10 = distinct !{!10, !11, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!11 = distinct !{!11, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!12 = distinct !{!12, !13, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!13 = distinct !{!13, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!14 = !{!15}
!15 = distinct !{!15, !16, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!16 = distinct !{!16, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!17 = distinct !{!17, !18, !19}
!18 = !{!"llvm.loop.isvectorized", i32 1}
!19 = !{!"llvm.loop.unroll.runtime.disable"}
!20 = distinct !{!20, !19, !18}
!21 = !{!22, !24, !26}
!22 = distinct !{!22, !23, !"_RINvNtNtCscliFh4jUES5_4core3str11validations15next_code_pointINtNtNtB6_5slice4iter4IterhEEB6_: %bytes"}
!23 = distinct !{!23, !"_RINvNtNtCscliFh4jUES5_4core3str11validations15next_code_pointINtNtNtB6_5slice4iter4IterhEEB6_"}
!24 = distinct !{!24, !25, !"_RNvXs3_NtNtCscliFh4jUES5_4core3str4iterNtB5_11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator4next: %self"}
!25 = distinct !{!25, !"_RNvXs3_NtNtCscliFh4jUES5_4core3str4iterNtB5_11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator4next"}
!26 = distinct !{!26, !27, !"_RINvYNtNtNtCscliFh4jUES5_4core3str4iter11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator8try_foldINtNtNtB9_3num7nonzero7NonZerojENCNvXs_NvBO_10advance_byB3_NtB2f_13SpecAdvanceBy15spec_advance_by0INtNtB9_6option6OptionB1C_EEB9_: %self"}
!27 = distinct !{!27, !"_RINvYNtNtNtCscliFh4jUES5_4core3str4iter11CharIndicesNtNtNtNtB9_4iter6traits8iterator8Iterator8try_foldINtNtNtB9_3num7nonzero7NonZerojENCNvXs_NvBO_10advance_byB3_NtB2f_13SpecAdvanceBy15spec_advance_by0INtNtB9_6option6OptionB1C_EEB9_"}
!28 = !{!29}
!29 = distinct !{!29, !30, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!30 = distinct !{!30, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!31 = !{!32}
!32 = distinct !{!32, !33, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write: %f"}
!33 = distinct !{!33, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write"}
!34 = !{!"branch_weights", i32 0, i32 -2147483648}
!35 = !{!36, !38, !40}
!36 = distinct !{!36, !37, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!37 = distinct !{!37, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!38 = distinct !{!38, !39, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!39 = distinct !{!39, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!40 = distinct !{!40, !41, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!41 = distinct !{!41, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!42 = !{!43}
!43 = distinct !{!43, !44, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!44 = distinct !{!44, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!45 = distinct !{!45, !18, !19}
!46 = distinct !{!46, !19, !18}
!47 = !{!48, !50, !52}
!48 = distinct !{!48, !49, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!49 = distinct !{!49, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!50 = distinct !{!50, !51, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!51 = distinct !{!51, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!52 = distinct !{!52, !53, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!53 = distinct !{!53, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!54 = !{!55}
!55 = distinct !{!55, !56, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!56 = distinct !{!56, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!57 = !{!58, !60, !62}
!58 = distinct !{!58, !59, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!59 = distinct !{!59, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!60 = distinct !{!60, !61, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!61 = distinct !{!61, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!62 = distinct !{!62, !63, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!63 = distinct !{!63, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!64 = !{!65}
!65 = distinct !{!65, !66, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!66 = distinct !{!66, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!67 = distinct !{!67, !18, !19}
!68 = distinct !{!68, !19, !18}
!69 = distinct !{!69, !18, !19}
!70 = distinct !{!70, !19, !18}
!71 = !{!72, !74, !76}
!72 = distinct !{!72, !73, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_: argument 0"}
!73 = distinct !{!73, !"_RNCINvNvXs1_NtNtNtCscliFh4jUES5_4core4iter8adapters6filterINtBa_6FilterppENtNtNtBe_6traits8iterator8Iterator5count8to_usizeRhNCNvNtNtBg_3str5count23char_count_general_case0E0Bg_"}
!74 = distinct !{!74, !75, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_: %elt"}
!75 = distinct !{!75, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters3map8map_foldRhjjNCINvNvXs1_NtB6_6filterINtB19_6FilterppENtNtNtB8_6traits8iterator8Iterator5count8to_usizeBU_NCNvNtNtBa_3str5count23char_count_general_case0E0NCINvXsK_NtB1G_5accumjNtB3o_3Sum3sumINtB4_3MapINtNtNtBa_5slice4iter4IterhEBY_EE0E0Ba_"}
!76 = distinct !{!76, !77, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case: %s.0"}
!77 = distinct !{!77, !"_RNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case"}
!78 = !{!79}
!79 = distinct !{!79, !80, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_: %_2"}
!80 = distinct !{!80, !"_RNCNvNtNtCscliFh4jUES5_4core3str5count23char_count_general_case0B7_"}
!81 = distinct !{!81, !18, !19}
!82 = distinct !{!82, !19, !18}
!83 = !{!84}
!84 = distinct !{!84, !85, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!85 = distinct !{!85, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!86 = !{!87}
!87 = distinct !{!87, !88, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write: %f"}
!88 = distinct !{!88, !"_RNvMs9_NtCscliFh4jUES5_4core3fmtNtB5_11PostPadding5write"}
!89 = !{!90}
!90 = distinct !{!90, !91, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding: %self"}
!91 = distinct !{!91, !"_RNvMsa_NtCscliFh4jUES5_4core3fmtNtB5_9Formatter7padding"}
!92 = !{!93}
!93 = distinct !{!93, !94, !"_RNvNtNtCscliFh4jUES5_4core3str11validations19run_utf8_validation: %v.0"}
!94 = distinct !{!94, !"_RNvNtNtCscliFh4jUES5_4core3str11validations19run_utf8_validation"}
!95 = !{!96}
!96 = distinct !{!96, !94, !"_RNvNtNtCscliFh4jUES5_4core3str11validations19run_utf8_validation: %_0"}
!97 = !{!96, !93}
!98 = !{!99}
!99 = distinct !{!99, !100, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!100 = distinct !{!100, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!101 = !{i8 0, i8 5}
!102 = !{!103, !104}
!103 = distinct !{!103, !100, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!104 = distinct !{!104, !100, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!105 = !{!103}
!106 = !{!103, !99, !104}
!107 = !{i8 0, i8 4}
!108 = !{!109}
!109 = distinct !{!109, !110, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!110 = distinct !{!110, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!111 = !{!112}
!112 = distinct !{!112, !113, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!113 = distinct !{!113, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!114 = !{!115}
!115 = distinct !{!115, !116, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!116 = distinct !{!116, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!117 = !{!118}
!118 = distinct !{!118, !119, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!119 = distinct !{!119, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!120 = !{!121}
!121 = distinct !{!121, !122, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!122 = distinct !{!122, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!123 = !{i64 0, i64 -9223372036854775808}
!124 = !{!121, !118, !115, !112, !109}
!125 = !{i8 0, i8 2}
!126 = !{!127}
!127 = distinct !{!127, !128, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!128 = distinct !{!128, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!129 = !{!130}
!130 = distinct !{!130, !128, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!131 = !{!132, !134}
!132 = distinct !{!132, !133, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_: %self"}
!133 = distinct !{!133, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_"}
!134 = distinct !{!134, !135, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerEEB1h_: %_1"}
!135 = distinct !{!135, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirConsumerEEB1h_"}
!136 = !{!137, !139}
!137 = distinct !{!137, !138, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_: %_0"}
!138 = distinct !{!138, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_"}
!139 = distinct !{!139, !138, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_consumer_open0B3_: %_1"}
!140 = !{!141, !143, !137, !139}
!141 = distinct !{!141, !142, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!142 = distinct !{!142, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!143 = distinct !{!143, !142, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4openNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!144 = !{!141, !137, !139}
!145 = !{i8 0, i8 6}
!146 = !{!143, !137, !139}
!147 = !{!"branch_weights", !"expected", i32 1, i32 2000}
!148 = !{i64 0, i64 2}
!149 = !{!150}
!150 = distinct !{!150, !151, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!151 = distinct !{!151, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!152 = !{!153}
!153 = distinct !{!153, !151, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4ringNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!154 = !{!155}
!155 = distinct !{!155, !156, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %_0"}
!156 = distinct !{!156, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi"}
!157 = !{!158}
!158 = distinct !{!158, !156, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %x"}
!159 = !{!155, !158, !160, !161}
!160 = distinct !{!160, !156, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %s"}
!161 = distinct !{!161, !156, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatNtB2_5FrameNCINvMs1_B2_Bz_4readB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: argument 3"}
!162 = !{!163, !158}
!163 = distinct !{!163, !164, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!164 = distinct !{!164, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!165 = !{!155, !160, !161}
!166 = !{i64 0, i64 3}
!167 = !{!158, !160, !161}
!168 = !{!169}
!169 = distinct !{!169, !170, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!170 = distinct !{!170, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!171 = !{!172}
!172 = distinct !{!172, !170, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!173 = !{!172, !158}
!174 = !{!169, !175, !155, !160, !161}
!175 = distinct !{!175, !170, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!176 = !{!169, !155}
!177 = !{!178, !172, !158}
!178 = distinct !{!178, !179, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!179 = distinct !{!179, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!180 = !{!172, !175, !155, !158, !160, !161}
!181 = !{!182}
!182 = distinct !{!182, !183, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!183 = distinct !{!183, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!184 = !{!185}
!185 = distinct !{!185, !186, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!186 = distinct !{!186, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!187 = !{!188}
!188 = distinct !{!188, !189, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!189 = distinct !{!189, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!190 = !{!191}
!191 = distinct !{!191, !192, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!192 = distinct !{!192, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!193 = !{!194}
!194 = distinct !{!194, !195, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!195 = distinct !{!195, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!196 = !{!194, !191, !188, !185, !182}
!197 = !{!198}
!198 = distinct !{!198, !199, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!199 = distinct !{!199, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!200 = !{!201}
!201 = distinct !{!201, !199, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!202 = !{!198, !201, !203, !204}
!203 = distinct !{!203, !199, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!204 = distinct !{!204, !199, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE8try_readNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %buf.0"}
!205 = !{!206}
!206 = distinct !{!206, !207, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!207 = distinct !{!207, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!208 = !{!209}
!209 = distinct !{!209, !207, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!210 = !{!209, !201}
!211 = !{!206, !212, !198, !203, !204}
!212 = distinct !{!212, !207, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4peekNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!213 = !{!209, !212, !198, !201, !203, !204}
!214 = !{!206, !209, !212, !198, !201, !203, !204}
!215 = !{!216}
!216 = distinct !{!216, !217, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!217 = distinct !{!217, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!218 = !{!219, !221, !222, !223, !206, !212, !198, !204}
!219 = distinct !{!219, !220, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_0"}
!220 = distinct !{!220, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi"}
!221 = distinct !{!221, !220, !"_RNCINvMs1_CsiYG6txRjdvL_9unir_edgeINtB8_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!222 = distinct !{!222, !217, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!223 = distinct !{!223, !217, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!224 = !{!219, !221, !222, !216, !223, !206, !209, !212, !198, !201, !203, !204}
!225 = !{i8 0, i8 7}
!226 = !{!216, !209, !201}
!227 = !{!222, !223, !206, !212, !198, !203, !204}
!228 = !{!206, !198, !204}
!229 = !{i8 0, i8 10}
!230 = !{!222, !216, !223, !206, !209, !212, !198, !201, !203, !204}
!231 = !{!198, !204}
!232 = !{!233, !209, !201}
!233 = distinct !{!233, !234, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!234 = distinct !{!234, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!235 = !{!236, !237, !206, !212, !198, !203, !204}
!236 = distinct !{!236, !234, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!237 = distinct !{!237, !234, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!238 = !{!236, !237, !206, !198, !204}
!239 = !{!201, !203, !204}
!240 = !{!198, !203, !204}
!241 = !{!242}
!242 = distinct !{!242, !243, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!243 = distinct !{!243, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!244 = !{!242, !201}
!245 = !{!246, !247, !198, !203, !204}
!246 = distinct !{!246, !243, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!247 = distinct !{!247, !243, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7advanceNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!248 = !{!246, !247, !198}
!249 = !{!246, !242, !247, !198, !201, !203, !204}
!250 = !{!251, !253, !246, !247, !198}
!251 = distinct !{!251, !252, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!252 = distinct !{!252, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!253 = distinct !{!253, !252, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!254 = !{!255, !242, !201}
!255 = distinct !{!255, !252, !"_RINvMs1_CsiYG6txRjdvL_9unir_edgeINtB6_8ConsumerNtCsIEB7taFyf8_10unir_temen11TemenRegionE7publishNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!256 = !{!251, !253, !246, !247, !198, !203, !204}
!257 = !{!246, !198}
!258 = !{!242, !247, !198, !201, !203, !204}
!259 = !{!260}
!260 = distinct !{!260, !261, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_: %_1"}
!261 = distinct !{!261, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_consumer_sever0B3_"}
!262 = !{!263}
!263 = distinct !{!263, !264, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!264 = distinct !{!264, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!265 = !{!263, !260}
!266 = !{!267, !268}
!267 = distinct !{!267, !264, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!268 = distinct !{!268, !264, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!269 = !{!267}
!270 = !{!267, !263, !268, !260}
!271 = !{!272}
!272 = distinct !{!272, !273, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!273 = distinct !{!273, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!274 = !{!275}
!275 = distinct !{!275, !276, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!276 = distinct !{!276, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!277 = !{!278}
!278 = distinct !{!278, !279, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!279 = distinct !{!279, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!280 = !{!281}
!281 = distinct !{!281, !282, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!282 = distinct !{!282, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!283 = !{!284}
!284 = distinct !{!284, !285, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!285 = distinct !{!285, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!286 = !{!284, !281, !278, !275, !272}
!287 = !{!288}
!288 = distinct !{!288, !289, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!289 = distinct !{!289, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!290 = !{!291, !292}
!291 = distinct !{!291, !289, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!292 = distinct !{!292, !289, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!293 = !{!291}
!294 = !{!291, !288, !292}
!295 = !{!296}
!296 = distinct !{!296, !297, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!297 = distinct !{!297, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!298 = !{!299}
!299 = distinct !{!299, !300, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!300 = distinct !{!300, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!301 = !{!302}
!302 = distinct !{!302, !303, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!303 = distinct !{!303, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!304 = !{!305}
!305 = distinct !{!305, !306, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!306 = distinct !{!306, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!307 = !{!308}
!308 = distinct !{!308, !309, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!309 = distinct !{!309, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!310 = !{!308, !305, !302, !299, !296}
!311 = !{!312, !314}
!312 = distinct !{!312, !313, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_: %self"}
!313 = distinct !{!313, !"_RNvXs8_NtCsksNX8Mxey3D_5alloc5boxedINtB5_3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropBJ_"}
!314 = distinct !{!314, !315, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerEEB1h_: %_1"}
!315 = distinct !{!315, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc5boxed3BoxNtCsdMVUwQQj4R6_9unir_cabi12UnirProducerEEB1h_"}
!316 = !{!317, !319}
!317 = distinct !{!317, !318, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_: %_0"}
!318 = distinct !{!318, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_"}
!319 = distinct !{!319, !318, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi18unir_producer_open0B3_: %_1"}
!320 = !{!321}
!321 = distinct !{!321, !322, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_: %_1"}
!322 = distinct !{!322, !"_RNCNvCsdMVUwQQj4R6_9unir_cabi19unir_producer_sever0B3_"}
!323 = !{!324}
!324 = distinct !{!324, !325, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!325 = distinct !{!325, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!326 = !{!324, !321}
!327 = !{!328, !329}
!328 = distinct !{!328, !325, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!329 = distinct !{!329, !325, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE3actNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!330 = !{!328}
!331 = !{!328, !324, !329, !321}
!332 = !{!333}
!333 = distinct !{!333, !334, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!334 = distinct !{!334, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!335 = !{!336}
!336 = distinct !{!336, !337, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!337 = distinct !{!337, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!338 = !{!339}
!339 = distinct !{!339, !340, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!340 = distinct !{!340, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!341 = !{!342}
!342 = distinct !{!342, !343, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!343 = distinct !{!343, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!344 = !{!345}
!345 = distinct !{!345, !346, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!346 = distinct !{!346, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!347 = !{!345, !342, !339, !336, !333}
!348 = !{!349}
!349 = distinct !{!349, !350, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %x"}
!350 = distinct !{!350, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi"}
!351 = !{!352, !349}
!352 = distinct !{!352, !353, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!353 = distinct !{!353, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!354 = !{!355, !356, !357}
!355 = distinct !{!355, !350, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %_0"}
!356 = distinct !{!356, !350, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %s"}
!357 = distinct !{!357, !350, !"_RINvCsiYG6txRjdvL_9unir_edge8blockingINtB2_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionEBO_NtBQ_8TemenVatuNCINvMs0_B2_Bz_5writeB1w_Es_0ECsdMVUwQQj4R6_9unir_cabi: %attempt"}
!358 = !{!355, !357}
!359 = !{!355, !349, !356, !357}
!360 = !{!355}
!361 = !{!357}
!362 = !{!363}
!363 = distinct !{!363, !364, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!364 = distinct !{!364, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!365 = !{!366}
!366 = distinct !{!366, !364, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!367 = !{!366, !349}
!368 = !{!363, !369, !355, !356, !357}
!369 = distinct !{!369, !364, !"_RINvMs_CsiYG6txRjdvL_9unir_edgeINtB5_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4parkNtBF_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!370 = !{!363, !355, !357}
!371 = !{!372, !366, !349}
!372 = distinct !{!372, !373, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi: %self"}
!373 = distinct !{!373, !"_RNvMs_CsiYG6txRjdvL_9unir_edgeINtB4_3EndNtCsIEB7taFyf8_10unir_temen11TemenRegionE4flagCsdMVUwQQj4R6_9unir_cabi"}
!374 = !{!366, !369, !355, !349, !356, !357}
!375 = !{!349, !356, !357}
!376 = !{!377}
!377 = distinct !{!377, !378, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_1"}
!378 = distinct !{!378, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!379 = !{!380}
!380 = distinct !{!380, !381, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!381 = distinct !{!381, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!382 = !{!383}
!383 = distinct !{!383, !384, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi: %_1"}
!384 = distinct !{!384, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecINtNtB4_6option6OptionTxmlEEEECsdMVUwQQj4R6_9unir_cabi"}
!385 = !{!386}
!386 = distinct !{!386, !387, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi: %self"}
!387 = distinct !{!387, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecINtNtCscliFh4jUES5_4core6option6OptionTxmlEEENtNtNtBR_3ops4drop4Drop4dropCsdMVUwQQj4R6_9unir_cabi"}
!388 = !{!389}
!389 = distinct !{!389, !390, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi: %self"}
!390 = distinct !{!390, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsdMVUwQQj4R6_9unir_cabi"}
!391 = !{!389, !386, !383, !380, !377}
!392 = !{i64 2}
!393 = !{!394}
!394 = distinct !{!394, !395, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!395 = distinct !{!395, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!396 = !{!397}
!397 = distinct !{!397, !395, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!398 = !{!394, !399, !400}
!399 = distinct !{!399, !395, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!400 = distinct !{!400, !395, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %payload.0"}
!401 = !{!397, !399, !400}
!402 = !{!394, !397, !399, !400}
!403 = !{!404}
!404 = distinct !{!404, !405, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %self"}
!405 = distinct !{!405, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi"}
!406 = !{!407, !409, !410, !411, !394, !399, !400}
!407 = distinct !{!407, !408, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_0"}
!408 = distinct !{!408, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi"}
!409 = distinct !{!409, !408, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBN_8TemenVatE0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!410 = distinct !{!410, !405, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %_0"}
!411 = distinct !{!411, !405, !"_RINvMs0_CsiYG6txRjdvL_9unir_edgeINtB6_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE4pollNtBL_8TemenVatECsdMVUwQQj4R6_9unir_cabi: %s"}
!412 = !{!407, !409, !410, !404, !411, !394, !397, !399, !400}
!413 = !{!404, !397}
!414 = !{!410, !411, !394, !399, !400}
!415 = !{!394, !400}
!416 = !{i8 0, i8 9}
!417 = !{!418, !394}
!418 = distinct !{!418, !419, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi: %_1"}
!419 = distinct !{!419, !"_RNCINvMs0_CsiYG6txRjdvL_9unir_edgeINtB8_8ProducerNtCsIEB7taFyf8_10unir_temen11TemenRegionE9try_writeNtBN_8TemenVatEs_0CsdMVUwQQj4R6_9unir_cabi"}
!420 = !{!421, !423, !425}
!421 = distinct !{!421, !422, !"_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner15try_allocate_inCsdMVUwQQj4R6_9unir_cabi: %_0"}
!422 = distinct !{!422, !"_RNvMs4_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner15try_allocate_inCsdMVUwQQj4R6_9unir_cabi"}
!423 = distinct !{!423, !424, !"_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi: %v"}
!424 = distinct !{!424, !"_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi"}
!425 = distinct !{!425, !424, !"_RINvXs_NvMNtCsksNX8Mxey3D_5alloc5sliceSp9to_vec_inhNtB5_10ConvertVec6to_vecNtNtBa_5alloc6GlobalECsdMVUwQQj4R6_9unir_cabi: %s.0"}
!426 = !{!423, !425}
!427 = !{!423}
!428 = !{!429}
!429 = distinct !{!429, !430, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangelENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_8find_map5checkllNCNvCsIEB7taFyf8_10unir_temen12instantiator0E0INtNtB8_12control_flow11ControlFlowlEEB25_: %self"}
!430 = distinct !{!430, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangelENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_8find_map5checkllNCNvCsIEB7taFyf8_10unir_temen12instantiator0E0INtNtB8_12control_flow11ControlFlowlEEB25_"}
!431 = !{!"branch_weights", i32 2002, i32 2000}
!432 = distinct !{!432, !433}
!433 = !{!"llvm.loop.unroll.disable"}
!434 = !{!"branch_weights", i32 4001, i32 4000000}
!435 = !{!436}
!436 = distinct !{!436, !437, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen: %dest.0"}
!437 = distinct !{!437, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen"}
!438 = !{!439}
!439 = distinct !{!439, !437, !"_RINvNtCscliFh4jUES5_4core5slice20copy_from_slice_implhECsIEB7taFyf8_10unir_temen: %src.0"}
!440 = distinct !{!440, !433}
!441 = !{i64 0, i64 -9223372036854775807}
!442 = !{!443}
!443 = distinct !{!443, !444, !"_RNvCsIEB7taFyf8_10unir_temen11region_size: %_0"}
!444 = distinct !{!444, !"_RNvCsIEB7taFyf8_10unir_temen11region_size"}
!445 = !{!446}
!446 = distinct !{!446, !447, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!447 = distinct !{!447, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!448 = !{!449}
!449 = distinct !{!449, !447, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!450 = !{!451}
!451 = distinct !{!451, !452, !"_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat10free_carve: %self"}
!452 = distinct !{!452, !"_RNvMs0_CsIEB7taFyf8_10unir_temenNtB5_8TemenVat10free_carve"}
!453 = !{!454}
!454 = distinct !{!454, !455, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangemENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_4find5checkmNCNvMs0_CsIEB7taFyf8_10unir_temenNtB24_8TemenVat10free_carve0E0INtNtB8_12control_flow11ControlFlowmEEB24_: argument 1"}
!455 = distinct !{!455, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangemENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_4find5checkmNCNvMs0_CsIEB7taFyf8_10unir_temenNtB24_8TemenVat10free_carve0E0INtNtB8_12control_flow11ControlFlowmEEB24_"}
!456 = !{!454, !451}
!457 = !{!458, !460, !461, !463}
!458 = distinct !{!458, !459, !"_RNCNvMs0_CsIEB7taFyf8_10unir_temenNtB7_8TemenVat10free_carve0B7_: %_1"}
!459 = distinct !{!459, !"_RNCNvMs0_CsIEB7taFyf8_10unir_temenNtB7_8TemenVat10free_carve0B7_"}
!460 = distinct !{!460, !459, !"_RNCNvMs0_CsIEB7taFyf8_10unir_temenNtB7_8TemenVat10free_carve0B7_: %s"}
!461 = distinct !{!461, !462, !"_RNCINvNvNtNtNtNtCscliFh4jUES5_4core4iter6traits8iterator8Iterator4find5checkmNCNvMs0_CsIEB7taFyf8_10unir_temenNtB1l_8TemenVat10free_carve0E0B1l_: %_1"}
!462 = distinct !{!462, !"_RNCINvNvNtNtNtNtCscliFh4jUES5_4core4iter6traits8iterator8Iterator4find5checkmNCNvMs0_CsIEB7taFyf8_10unir_temenNtB1l_8TemenVat10free_carve0E0B1l_"}
!463 = distinct !{!463, !455, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangemENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_4find5checkmNCNvMs0_CsIEB7taFyf8_10unir_temenNtB24_8TemenVat10free_carve0E0INtNtB8_12control_flow11ControlFlowmEEB24_: %self"}
!464 = !{!465, !467}
!465 = distinct !{!465, !466, !"_RNvXs9_NtCscliFh4jUES5_4core6optionRINtB5_6OptionTxmlEENtNtNtNtB7_4iter6traits7collect12IntoIterator9into_iterCsIEB7taFyf8_10unir_temen: %self"}
!466 = distinct !{!466, !"_RNvXs9_NtCscliFh4jUES5_4core6optionRINtB5_6OptionTxmlEENtNtNtNtB7_4iter6traits7collect12IntoIterator9into_iterCsIEB7taFyf8_10unir_temen"}
!467 = distinct !{!467, !468, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters7flatten15try_flatten_oneRINtNtBa_6option6OptionTxmlEEuINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvNtNtNtB8_6traits8iterator8Iterator3any5checkRB1t_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB3l_8TemenVat10free_carve00E0E0B3l_: %inner"}
!468 = distinct !{!468, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters7flatten15try_flatten_oneRINtNtBa_6option6OptionTxmlEEuINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvNtNtNtB8_6traits8iterator8Iterator3any5checkRB1t_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB3l_8TemenVat10free_carve00E0E0B3l_"}
!469 = !{!470, !471, !473, !474, !476, !458, !460, !461, !463, !454, !451}
!470 = distinct !{!470, !468, !"_RNCINvNtNtNtCscliFh4jUES5_4core4iter8adapters7flatten15try_flatten_oneRINtNtBa_6option6OptionTxmlEEuINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvNtNtNtB8_6traits8iterator8Iterator3any5checkRB1t_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB3l_8TemenVat10free_carve00E0E0B3l_: %_1"}
!471 = distinct !{!471, !472, !"_RINvYINtNtNtCscliFh4jUES5_4core5slice4iter4IterINtNtBa_6option6OptionTxmlEEENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNtNtB1i_8adapters7flatten15try_flatten_oneRBJ_uINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvB1c_3any5checkRB15_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB48_8TemenVat10free_carve00E0E0B2R_EB48_: %self"}
!472 = distinct !{!472, !"_RINvYINtNtNtCscliFh4jUES5_4core5slice4iter4IterINtNtBa_6option6OptionTxmlEEENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNtNtB1i_8adapters7flatten15try_flatten_oneRBJ_uINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvB1c_3any5checkRB15_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB48_8TemenVat10free_carve00E0E0B2R_EB48_"}
!473 = distinct !{!473, !472, !"_RINvYINtNtNtCscliFh4jUES5_4core5slice4iter4IterINtNtBa_6option6OptionTxmlEEENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNtNtB1i_8adapters7flatten15try_flatten_oneRBJ_uINtNtNtBa_3ops12control_flow11ControlFlowuENCINvNvB1c_3any5checkRB15_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB48_8TemenVat10free_carve00E0E0B2R_EB48_: argument 1"}
!474 = distinct !{!474, !475, !"_RINvXs9_NtNtNtCscliFh4jUES5_4core4iter8adapters4fuseINtB6_4FuseINtNtNtBc_5slice4iter4IterINtNtBc_6option6OptionTxmlEEEEINtB6_8FuseImplBZ_E8try_folduNCINvNtB8_7flatten15try_flatten_oneRB1p_uINtNtNtBc_3ops12control_flow11ControlFlowuENCINvNvNtNtNtBa_6traits8iterator8Iterator3any5checkRB1L_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB4M_8TemenVat10free_carve00E0E0B31_EB4M_: %self"}
!475 = distinct !{!475, !"_RINvXs9_NtNtNtCscliFh4jUES5_4core4iter8adapters4fuseINtB6_4FuseINtNtNtBc_5slice4iter4IterINtNtBc_6option6OptionTxmlEEEEINtB6_8FuseImplBZ_E8try_folduNCINvNtB8_7flatten15try_flatten_oneRB1p_uINtNtNtBc_3ops12control_flow11ControlFlowuENCINvNvNtNtNtBa_6traits8iterator8Iterator3any5checkRB1L_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB4M_8TemenVat10free_carve00E0E0B31_EB4M_"}
!476 = distinct !{!476, !475, !"_RINvXs9_NtNtNtCscliFh4jUES5_4core4iter8adapters4fuseINtB6_4FuseINtNtNtBc_5slice4iter4IterINtNtBc_6option6OptionTxmlEEEEINtB6_8FuseImplBZ_E8try_folduNCINvNtB8_7flatten15try_flatten_oneRB1p_uINtNtNtBc_3ops12control_flow11ControlFlowuENCINvNvNtNtNtBa_6traits8iterator8Iterator3any5checkRB1L_NCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB4M_8TemenVat10free_carve00E0E0B31_EB4M_: %fold"}
!477 = !{!478, !480, !467}
!478 = distinct !{!478, !479, !"_RNCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB9_8TemenVat10free_carve00B9_: %_2"}
!479 = distinct !{!479, !"_RNCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB9_8TemenVat10free_carve00B9_"}
!480 = distinct !{!480, !481, !"_RNCINvNvNtNtNtNtCscliFh4jUES5_4core4iter6traits8iterator8Iterator3any5checkRTxmlENCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB1r_8TemenVat10free_carve00E0B1r_: %x"}
!481 = distinct !{!481, !"_RNCINvNvNtNtNtNtCscliFh4jUES5_4core4iter6traits8iterator8Iterator3any5checkRTxmlENCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB1r_8TemenVat10free_carve00E0B1r_"}
!482 = !{!483, !484, !470, !471, !473, !474, !476, !458, !460, !461, !463, !454, !451}
!483 = distinct !{!483, !479, !"_RNCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB9_8TemenVat10free_carve00B9_: %_1"}
!484 = distinct !{!484, !481, !"_RNCINvNvNtNtNtNtCscliFh4jUES5_4core4iter6traits8iterator8Iterator3any5checkRTxmlENCNCNvMs0_CsIEB7taFyf8_10unir_temenNtB1r_8TemenVat10free_carve00E0B1r_: %_1"}
!485 = !{!486}
!486 = distinct !{!486, !487, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_: %_1"}
!487 = distinct !{!487, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_"}
!488 = !{!489}
!489 = distinct !{!489, !490, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VechEECsIEB7taFyf8_10unir_temen: %_1"}
!490 = distinct !{!490, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VechEECsIEB7taFyf8_10unir_temen"}
!491 = !{!492}
!492 = distinct !{!492, !493, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVechEECsIEB7taFyf8_10unir_temen: %_1"}
!493 = distinct !{!493, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVechEECsIEB7taFyf8_10unir_temen"}
!494 = !{!495}
!495 = distinct !{!495, !496, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVechENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen: %self"}
!496 = distinct !{!496, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVechENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen"}
!497 = !{!498}
!498 = distinct !{!498, !499, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen: %self"}
!499 = distinct !{!499, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen"}
!500 = !{!498, !495, !492, !489, !486}
!501 = !{!502}
!502 = distinct !{!502, !503, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangelENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_8find_map5checkllNCNvCsIEB7taFyf8_10unir_temen12instantiator0E0INtNtB8_12control_flow11ControlFlowlEEB25_: %self"}
!503 = distinct !{!503, !"_RINvYINtNtNtCscliFh4jUES5_4core3ops5range5RangelENtNtNtNtBa_4iter6traits8iterator8Iterator8try_folduNCINvNvBL_8find_map5checkllNCNvCsIEB7taFyf8_10unir_temen12instantiator0E0INtNtB8_12control_flow11ControlFlowlEEB25_"}
!504 = !{!505}
!505 = distinct !{!505, !506, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen: %_1"}
!506 = distinct !{!506, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen"}
!507 = !{!508}
!508 = distinct !{!508, !509, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecAmj4_EECsIEB7taFyf8_10unir_temen: %_1"}
!509 = distinct !{!509, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecAmj4_EECsIEB7taFyf8_10unir_temen"}
!510 = !{!511}
!511 = distinct !{!511, !512, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_ENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen: %self"}
!512 = distinct !{!512, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_ENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen"}
!513 = !{!514}
!514 = distinct !{!514, !515, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen: %self"}
!515 = distinct !{!515, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen"}
!516 = !{!514, !511, !508, !505}
!517 = !{!518, !520}
!518 = distinct !{!518, !519, !"_RNvMNtCscliFh4jUES5_4core6optionINtB2_6OptionTxmlEE7is_noneCsIEB7taFyf8_10unir_temen: %self"}
!519 = distinct !{!519, !"_RNvMNtCscliFh4jUES5_4core6optionINtB2_6OptionTxmlEE7is_noneCsIEB7taFyf8_10unir_temen"}
!520 = distinct !{!520, !521, !"_RNvYNvMNtCscliFh4jUES5_4core6optionINtB5_6OptionTxmlEE7is_noneINtNtNtB7_3ops8function5FnMutTRBx_EE8call_mutCsIEB7taFyf8_10unir_temen: argument 0"}
!521 = distinct !{!521, !"_RNvYNvMNtCscliFh4jUES5_4core6optionINtB5_6OptionTxmlEE7is_noneINtNtNtB7_3ops8function5FnMutTRBx_EE8call_mutCsIEB7taFyf8_10unir_temen"}
!522 = !{!523}
!523 = distinct !{!523, !524, !"_RINvXs2J_NtNtCscliFh4jUES5_4core5slice4iterINtB7_4IterINtNtBb_6option6OptionTxmlEEENtNtNtNtBb_4iter6traits8iterator8Iterator8positionNvMBT_BQ_7is_noneECsIEB7taFyf8_10unir_temen: %self"}
!524 = distinct !{!524, !"_RINvXs2J_NtNtCscliFh4jUES5_4core5slice4iterINtB7_4IterINtNtBb_6option6OptionTxmlEEENtNtNtNtBb_4iter6traits8iterator8Iterator8positionNvMBT_BQ_7is_noneECsIEB7taFyf8_10unir_temen"}
!525 = !{!526}
!526 = distinct !{!526, !527, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen: %_1"}
!527 = distinct !{!527, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VecAmj4_EECsIEB7taFyf8_10unir_temen"}
!528 = !{!529}
!529 = distinct !{!529, !530, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecAmj4_EECsIEB7taFyf8_10unir_temen: %_1"}
!530 = distinct !{!530, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVecAmj4_EECsIEB7taFyf8_10unir_temen"}
!531 = !{!532}
!532 = distinct !{!532, !533, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_ENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen: %self"}
!533 = distinct !{!533, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVecAmj4_ENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen"}
!534 = !{!535}
!535 = distinct !{!535, !536, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen: %self"}
!536 = distinct !{!536, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen"}
!537 = !{!535, !532, !529, !526}
!538 = !{!539}
!539 = distinct !{!539, !540, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_: %_1"}
!540 = distinct !{!540, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeNtCsIEB7taFyf8_10unir_temen12TemenProgramEBI_"}
!541 = !{!542}
!542 = distinct !{!542, !543, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VechEECsIEB7taFyf8_10unir_temen: %_1"}
!543 = distinct !{!543, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc3vec3VechEECsIEB7taFyf8_10unir_temen"}
!544 = !{!545}
!545 = distinct !{!545, !546, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVechEECsIEB7taFyf8_10unir_temen: %_1"}
!546 = distinct !{!546, !"_RINvNtCscliFh4jUES5_4core3ptr13drop_in_placeINtNtCsksNX8Mxey3D_5alloc7raw_vec6RawVechEECsIEB7taFyf8_10unir_temen"}
!547 = !{!548}
!548 = distinct !{!548, !549, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVechENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen: %self"}
!549 = distinct !{!549, !"_RNvXs1_NtCsksNX8Mxey3D_5alloc7raw_vecINtB5_6RawVechENtNtNtCscliFh4jUES5_4core3ops4drop4Drop4dropCsIEB7taFyf8_10unir_temen"}
!550 = !{!551}
!551 = distinct !{!551, !552, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen: %self"}
!552 = distinct !{!552, !"_RNvMs2_NtCsksNX8Mxey3D_5alloc7raw_vecNtB5_11RawVecInner10deallocateCsIEB7taFyf8_10unir_temen"}
!553 = !{!551, !548, !545, !542, !539}
!554 = !{!555}
!555 = distinct !{!555, !556, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32: %self"}
!556 = distinct !{!556, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u32"}
!557 = !{!558}
!558 = distinct !{!558, !559, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64: %self"}
!559 = distinct !{!559, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64"}
!560 = !{!561}
!561 = distinct !{!561, !562, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64: %self"}
!562 = distinct !{!562, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64"}
!563 = !{!564}
!564 = distinct !{!564, !565, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64: %self"}
!565 = distinct !{!565, !"_RNvXs_CsIEB7taFyf8_10unir_temenNtB4_11TemenRegionNtCsc1i3HueRnCr_14unir_substrate6Region9store_u64"}
!566 = distinct !{!566, !433}
!567 = distinct !{!567, !433}
!568 = !{!569}
!569 = distinct !{!569, !570, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!570 = distinct !{!570, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!571 = !{!572}
!572 = distinct !{!572, !570, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!573 = !{!574}
!574 = distinct !{!574, !575, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!575 = distinct !{!575, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!576 = !{!577}
!577 = distinct !{!577, !575, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!578 = !{!579}
!579 = distinct !{!579, !580, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!580 = distinct !{!580, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!581 = !{!582}
!582 = distinct !{!582, !580, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!583 = !{!584}
!584 = distinct !{!584, !585, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!585 = distinct !{!585, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!586 = !{!587}
!587 = distinct !{!587, !585, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
!588 = !{!589}
!589 = distinct !{!589, !590, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %self"}
!590 = distinct !{!590, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word"}
!591 = !{!592}
!592 = distinct !{!592, !590, !"_RNvMCsIEB7taFyf8_10unir_temenNtB2_11TemenRegion4word: %_0"}
