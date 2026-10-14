// src/Support/FormName.h
#pragma once

#include <RE/T/TESForm.h>

namespace Support {

// RE::TESForm::GetName() 对匿名对象返回 nullptr；直接喂给 fmt 会对
// 空指针取 strlen（UB），日志路径自己崩了就什么都诊断不到。
//
// 返回的指针指向 <unnamed> 字面量（静态存储期）或 form 内部的
// 字符串，调用方不得修改，生命周期与 form 一致。
[[nodiscard]] inline auto SafeName(const RE::TESForm* a_form) -> const char* {
    const char* name = a_form->GetName();
    return name != nullptr ? name : "<unnamed>";
}

}  // namespace Support
