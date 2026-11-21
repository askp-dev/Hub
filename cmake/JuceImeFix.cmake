# Windows: stop a stack-overflow crash in hosts such as Ableton Live.
# JUCE 8.0.4 answers WM_IME_SETCONTEXT with ImmIsUIMessage(), which sends the message
# back out of the plug-in window; Ableton sends it straight back in, and the two bounce
# it until the host's main-thread stack runs out. JUCE fixed this upstream (commit
# 038b0d6c9, in 8.0.5) by letting DefWindowProc handle it. Apply the same one-line fix
# to the fetched source at configure time; it checks whether it is already applied and
# fails loudly if JUCE changes under it.
function(askp_patch_juce_ime juce_dir)
    set(file "${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
    file(READ "${file}" src)
    set(marker "askp patch: IME setcontext recursion")
    string(FIND "${src}" "${marker}" done)
    if(done EQUAL -1)
        set(anchor "return ImmIsUIMessage (h, message, wParam, lParam);")
        string(FIND "${src}" "${anchor}" at)
        if(at EQUAL -1)
            string(FIND "${src}" "case WM_IME_SETCONTEXT:" has_case)
            if(has_case EQUAL -1)
                message(FATAL_ERROR "JuceImeFix: WM_IME_SETCONTEXT handler not found; check the fix against this JUCE version")
            endif()
            message(STATUS "JuceImeFix: this JUCE already has the upstream IME fix")
            return()
        endif()
        string(REPLACE "${anchor}" "break;   // ${marker}" src "${src}")
        file(WRITE "${file}" "${src}")
        message(STATUS "JuceImeFix: applied WM_IME_SETCONTEXT fix")
    endif()
endfunction()
