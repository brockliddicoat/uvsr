;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; no parameters
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; no parameters
; shader hash: 231b114ce62f648c180c555a4bbab7e7
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Compute Shader
; NumThreads=(8,8,1)
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 0
; SigOutputElements: 0
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 0
; SigOutputVectors[0]: 0
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: main
;
;
; Buffer Definitions:
;
; cbuffer c_Extract
; {
;
;   struct c_Extract
;   {
;
;       uint g_Width;                                 ; Offset:    0
;       uint g_Height;                                ; Offset:    4
;       uint g_SampleCount;                           ; Offset:    8
;       uint g_Reserved;                              ; Offset:   12
;   
;   } c_Extract;                                      ; Offset:    0 Size:    16
;
; }
;
; Resource bind info for u_Samples
; {
;
;   struct struct.ExtractedSample
;   {
;
;       uint2 surfaceKey;                             ; Offset:    0
;       uint visibilityClass;                         ; Offset:    8
;       uint depthBits;                               ; Offset:   12
;   
;   } $Element;                                       ; Offset:    0 Size:    16
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; c_Extract                         cbuffer      NA          NA     CB0            cb0     1
; t_SurfaceKeys                     texture     u32       2dMS2      T0             t0     1
; t_VisibilityClass                 texture     u32       2dMS2      T1             t1     1
; t_Depth                           texture     f32       2dMS2      T2             t2     1
; u_Samples                             UAV  struct         r/w      U0             u0     1
; u_Counters                            UAV    byte         r/w      U1             u1     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.Dimensions = type { i32, i32, i32, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2DMS<vector<unsigned int, 2>, 2>" = type { <2 x i32>, %"class.Texture2DMS<vector<unsigned int, 2>, 2>::sample_type" }
%"class.Texture2DMS<vector<unsigned int, 2>, 2>::sample_type" = type { i32 }
%"class.Texture2DMS<unsigned int, 2>" = type { i32, %"class.Texture2DMS<unsigned int, 2>::sample_type" }
%"class.Texture2DMS<unsigned int, 2>::sample_type" = type { i32 }
%"class.Texture2DMS<float, 2>" = type { float, %"class.Texture2DMS<float, 2>::sample_type" }
%"class.Texture2DMS<float, 2>::sample_type" = type { i32 }
%"class.RWStructuredBuffer<ExtractedSample>" = type { %struct.ExtractedSample }
%struct.ExtractedSample = type { <2 x i32>, i32, i32 }
%struct.RWByteAddressBuffer = type { i32 }
%c_Extract = type { i32, i32, i32, i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 2, i32 2, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %7 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %8 = call i32 @dx.op.threadId.i32(i32 93, i32 1)  ; ThreadId(component)
  %9 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %2, i32 undef)  ; GetDimensions(handle,mipLevel)
  %10 = extractvalue %dx.types.Dimensions %9, 0
  %11 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %1, i32 undef)  ; GetDimensions(handle,mipLevel)
  %12 = extractvalue %dx.types.Dimensions %11, 0
  %13 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %5, i32 undef)  ; GetDimensions(handle,mipLevel)
  %14 = extractvalue %dx.types.Dimensions %13, 0
  %15 = extractvalue %dx.types.Dimensions %13, 1
  %16 = extractvalue %dx.types.Dimensions %13, 3
  %17 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %4, i32 undef)  ; GetDimensions(handle,mipLevel)
  %18 = extractvalue %dx.types.Dimensions %17, 0
  %19 = extractvalue %dx.types.Dimensions %17, 1
  %20 = extractvalue %dx.types.Dimensions %17, 3
  %21 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %3, i32 undef)  ; GetDimensions(handle,mipLevel)
  %22 = extractvalue %dx.types.Dimensions %21, 0
  %23 = extractvalue %dx.types.Dimensions %21, 1
  %24 = extractvalue %dx.types.Dimensions %21, 3
  %25 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %6, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.i32 %25, 0
  %27 = icmp eq i32 %26, 0
  br i1 %27, label %34, label %28

; <label>:28                                      ; preds = %0
  %29 = extractvalue %dx.types.CBufRet.i32 %25, 1
  %30 = icmp eq i32 %29, 0
  br i1 %30, label %34, label %31

; <label>:31                                      ; preds = %28
  %32 = udiv i32 -1, %29
  %33 = icmp ule i32 %26, %32
  br label %34

; <label>:34                                      ; preds = %31, %28, %0
  %35 = phi i1 [ false, %28 ], [ false, %0 ], [ %33, %31 ]
  %36 = extractvalue %dx.types.CBufRet.i32 %25, 1
  %37 = mul i32 %36, %26
  %38 = select i1 %35, i32 %37, i32 0
  br i1 %35, label %39, label %45

; <label>:39                                      ; preds = %34
  %40 = extractvalue %dx.types.CBufRet.i32 %25, 2
  %41 = icmp eq i32 %40, 0
  br i1 %41, label %45, label %42

; <label>:42                                      ; preds = %39
  %43 = udiv i32 -1, %40
  %44 = icmp ule i32 %38, %43
  br label %45

; <label>:45                                      ; preds = %42, %39, %34
  %46 = phi i1 [ false, %39 ], [ false, %34 ], [ %44, %42 ]
  br i1 %46, label %47, label %76

; <label>:47                                      ; preds = %45
  %48 = extractvalue %dx.types.CBufRet.i32 %25, 2
  %49 = mul i32 %38, %48
  %50 = select i1 %46, i32 %49, i32 0
  %51 = extractvalue %dx.types.CBufRet.i32 %25, 2
  %52 = icmp eq i32 %51, 2
  %53 = icmp eq i32 %10, %50
  %54 = and i1 %52, %53
  %55 = icmp eq i32 %12, 36
  %56 = and i1 %55, %54
  %57 = icmp eq i32 %14, %26
  %58 = and i1 %56, %57
  %59 = extractvalue %dx.types.CBufRet.i32 %25, 1
  %60 = icmp eq i32 %15, %59
  %61 = and i1 %58, %60
  %62 = icmp eq i32 %16, 2
  %63 = and i1 %61, %62
  %64 = icmp eq i32 %18, %14
  %65 = and i1 %63, %64
  %66 = icmp eq i32 %19, %15
  %67 = and i1 %65, %66
  %68 = icmp eq i32 %20, 2
  %69 = and i1 %67, %68
  %70 = icmp eq i32 %22, %14
  %71 = and i1 %69, %70
  %72 = icmp eq i32 %23, %15
  %73 = and i1 %71, %72
  %74 = icmp eq i32 %24, 2
  %75 = and i1 %73, %74
  br label %76

; <label>:76                                      ; preds = %47, %45
  %77 = phi i1 [ false, %45 ], [ %75, %47 ]
  br i1 %77, label %85, label %78

; <label>:78                                      ; preds = %76
  %79 = or i32 %8, %7
  %80 = icmp eq i32 %79, 0
  %81 = icmp ugt i32 %12, 27
  %82 = and i1 %80, %81
  br i1 %82, label %83, label %244

; <label>:83                                      ; preds = %78
  %84 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 24, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %244

; <label>:85                                      ; preds = %76
  %86 = extractvalue %dx.types.CBufRet.i32 %25, 1
  %87 = icmp uge i32 %7, %26
  %88 = icmp uge i32 %8, %86
  %89 = or i1 %87, %88
  br i1 %89, label %244, label %90

; <label>:90                                      ; preds = %85
  %91 = mul i32 %26, %8
  %92 = add i32 %91, %7
  %93 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %5, i32 0, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %94 = extractvalue %dx.types.ResRet.i32 %93, 0
  %95 = extractvalue %dx.types.ResRet.i32 %93, 1
  %96 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %4, i32 0, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %97 = extractvalue %dx.types.ResRet.i32 %96, 0
  %98 = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %3, i32 0, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %99 = extractvalue %dx.types.ResRet.f32 %98, 0
  %100 = bitcast float %99 to i32
  %101 = shl i32 %92, 1
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %101, i32 0, i32 %94, i32 %95, i32 undef, i32 undef, i8 3, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %101, i32 8, i32 %97, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %101, i32 12, i32 %100, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  %102 = call i1 @dx.op.isSpecialFloat.f32(i32 10, float %99)  ; IsFinite(value)
  %103 = fcmp fast ogt float %99, 0.000000e+00
  %104 = and i1 %103, %102
  %105 = icmp eq i32 %94, -1
  %106 = icmp eq i32 %95, -1
  %107 = lshr i32 %94, 20
  %108 = or i1 %105, %106
  %109 = xor i1 %108, true
  %110 = icmp ne i32 %107, 0
  %111 = and i1 %110, %109
  %112 = icmp ne i32 %107, 4095
  %113 = and i1 %112, %111
  %114 = xor i1 %105, %106
  %115 = and i1 %113, %104
  br i1 %115, label %116, label %133

; <label>:116                                     ; preds = %90
  %117 = icmp eq i32 %97, 1
  br i1 %117, label %133, label %118

; <label>:118                                     ; preds = %116
  %119 = and i32 %97, 192
  %120 = and i32 %97, 63
  %121 = add nsw i32 %120, -15
  %122 = icmp ult i32 %121, 2
  %123 = icmp eq i32 %120, 18
  %124 = or i1 %123, %122
  %125 = and i32 %97, -128
  %126 = icmp eq i32 %125, 128
  %127 = add nsw i32 %120, -1
  %128 = icmp ult i32 %127, 21
  %129 = and i1 %126, %128
  %130 = icmp ne i32 %119, 192
  %131 = or i1 %130, %124
  %132 = and i1 %129, %131
  br label %133

; <label>:133                                     ; preds = %118, %116, %90
  %134 = phi i1 [ false, %90 ], [ true, %116 ], [ %132, %118 ]
  %135 = icmp eq i32 %100, 0
  br i1 %135, label %136, label %140

; <label>:136                                     ; preds = %133
  %137 = or i32 %94, %95
  %138 = or i32 %137, %97
  %139 = icmp eq i32 %138, 0
  br label %140

; <label>:140                                     ; preds = %136, %133
  %141 = phi i1 [ false, %133 ], [ %139, %136 ]
  br i1 %134, label %145, label %142

; <label>:142                                     ; preds = %140
  br i1 %141, label %143, label %147

; <label>:143                                     ; preds = %142
  %144 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 4, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %147

; <label>:145                                     ; preds = %140
  %146 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 0, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %147

; <label>:147                                     ; preds = %145, %143, %142
  br i1 %102, label %150, label %148

; <label>:148                                     ; preds = %147
  %149 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 8, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %150

; <label>:150                                     ; preds = %148, %147
  br i1 %114, label %151, label %153

; <label>:151                                     ; preds = %150
  %152 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 12, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %153

; <label>:153                                     ; preds = %151, %150
  %154 = xor i1 %104, true
  %155 = or i1 %134, %154
  br i1 %155, label %158, label %156

; <label>:156                                     ; preds = %153
  %157 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 16, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %158

; <label>:158                                     ; preds = %156, %153
  %159 = or i1 %104, %141
  br i1 %159, label %162, label %160

; <label>:160                                     ; preds = %158
  %161 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 20, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %162

; <label>:162                                     ; preds = %160, %158
  %163 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 28, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  %164 = fcmp fast olt float %99, 0.000000e+00
  %165 = and i1 %164, %102
  br i1 %165, label %166, label %168

; <label>:166                                     ; preds = %162
  %167 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 32, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %168

; <label>:168                                     ; preds = %166, %162
  %169 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %5, i32 1, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %170 = extractvalue %dx.types.ResRet.i32 %169, 0
  %171 = extractvalue %dx.types.ResRet.i32 %169, 1
  %172 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %4, i32 1, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %173 = extractvalue %dx.types.ResRet.i32 %172, 0
  %174 = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %3, i32 1, i32 %7, i32 %8, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %175 = extractvalue %dx.types.ResRet.f32 %174, 0
  %176 = bitcast float %175 to i32
  %177 = or i32 %101, 1
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %177, i32 0, i32 %170, i32 %171, i32 undef, i32 undef, i8 3, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %177, i32 8, i32 %173, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %2, i32 %177, i32 12, i32 %176, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  %178 = call i1 @dx.op.isSpecialFloat.f32(i32 10, float %175)  ; IsFinite(value)
  %179 = fcmp fast ogt float %175, 0.000000e+00
  %180 = and i1 %179, %178
  %181 = icmp eq i32 %170, -1
  %182 = icmp eq i32 %171, -1
  %183 = lshr i32 %170, 20
  %184 = or i1 %181, %182
  %185 = xor i1 %184, true
  %186 = icmp ne i32 %183, 0
  %187 = and i1 %186, %185
  %188 = icmp ne i32 %183, 4095
  %189 = and i1 %188, %187
  %190 = xor i1 %181, %182
  %191 = and i1 %189, %180
  br i1 %191, label %192, label %209

; <label>:192                                     ; preds = %168
  %193 = icmp eq i32 %173, 1
  br i1 %193, label %209, label %194

; <label>:194                                     ; preds = %192
  %195 = and i32 %173, 192
  %196 = and i32 %173, 63
  %197 = add nsw i32 %196, -15
  %198 = icmp ult i32 %197, 2
  %199 = icmp eq i32 %196, 18
  %200 = or i1 %199, %198
  %201 = and i32 %173, -128
  %202 = icmp eq i32 %201, 128
  %203 = add nsw i32 %196, -1
  %204 = icmp ult i32 %203, 21
  %205 = and i1 %202, %204
  %206 = icmp ne i32 %195, 192
  %207 = or i1 %206, %200
  %208 = and i1 %205, %207
  br label %209

; <label>:209                                     ; preds = %194, %192, %168
  %210 = phi i1 [ false, %168 ], [ true, %192 ], [ %208, %194 ]
  %211 = icmp eq i32 %176, 0
  br i1 %211, label %212, label %216

; <label>:212                                     ; preds = %209
  %213 = or i32 %170, %171
  %214 = or i32 %213, %173
  %215 = icmp eq i32 %214, 0
  br label %216

; <label>:216                                     ; preds = %212, %209
  %217 = phi i1 [ false, %209 ], [ %215, %212 ]
  br i1 %210, label %221, label %218

; <label>:218                                     ; preds = %216
  br i1 %217, label %219, label %223

; <label>:219                                     ; preds = %218
  %220 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 4, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %223

; <label>:221                                     ; preds = %216
  %222 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 0, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %223

; <label>:223                                     ; preds = %221, %219, %218
  br i1 %178, label %226, label %224

; <label>:224                                     ; preds = %223
  %225 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 8, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %226

; <label>:226                                     ; preds = %224, %223
  br i1 %190, label %227, label %229

; <label>:227                                     ; preds = %226
  %228 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 12, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %229

; <label>:229                                     ; preds = %227, %226
  %230 = xor i1 %180, true
  %231 = or i1 %210, %230
  br i1 %231, label %234, label %232

; <label>:232                                     ; preds = %229
  %233 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 16, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %234

; <label>:234                                     ; preds = %232, %229
  %235 = or i1 %180, %217
  br i1 %235, label %238, label %236

; <label>:236                                     ; preds = %234
  %237 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 20, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %238

; <label>:238                                     ; preds = %236, %234
  %239 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 28, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  %240 = fcmp fast olt float %175, 0.000000e+00
  %241 = and i1 %240, %178
  br i1 %241, label %242, label %244

; <label>:242                                     ; preds = %238
  %243 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 32, i32 undef, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %244

; <label>:244                                     ; preds = %242, %238, %85, %83, %78
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.Dimensions @dx.op.getDimensions(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind
declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32) #1

; Function Attrs: nounwind
declare void @dx.op.rawBufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8, i32) #2

; Function Attrs: nounwind readnone
declare i1 @dx.op.isSpecialFloat.f32(i32, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!17}

!0 = !{!"dxcoob 1.9.2602.17 (21d28f727)"}
!1 = !{i32 1, i32 5}
!2 = !{i32 1, i32 9}
!3 = !{!"cs", i32 6, i32 5}
!4 = !{!5, !11, !15, null}
!5 = !{!6, !8, !9}
!6 = !{i32 0, %"class.Texture2DMS<vector<unsigned int, 2>, 2>"* undef, !"", i32 0, i32 0, i32 1, i32 3, i32 2, !7}
!7 = !{i32 0, i32 5}
!8 = !{i32 1, %"class.Texture2DMS<unsigned int, 2>"* undef, !"", i32 0, i32 1, i32 1, i32 3, i32 2, !7}
!9 = !{i32 2, %"class.Texture2DMS<float, 2>"* undef, !"", i32 0, i32 2, i32 1, i32 3, i32 2, !10}
!10 = !{i32 0, i32 9}
!11 = !{!12, !14}
!12 = !{i32 0, %"class.RWStructuredBuffer<ExtractedSample>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !13}
!13 = !{i32 1, i32 16}
!14 = !{i32 1, %struct.RWByteAddressBuffer* undef, !"", i32 0, i32 1, i32 1, i32 11, i1 false, i1 false, i1 false, null}
!15 = !{!16}
!16 = !{i32 0, %c_Extract* undef, !"", i32 0, i32 0, i32 1, i32 16, null}
!17 = !{void ()* @main, !"main", null, !4, !18}
!18 = !{i32 0, i64 16, i32 4, !19}
!19 = !{i32 8, i32 8, i32 1}
