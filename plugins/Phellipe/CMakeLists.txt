dpf_add_plugin(phellipe
  TARGETS lv2 vst3 clap jack
  UI_TYPE cairo
  FILES_DSP
      PhellipePlugin.cpp
  FILES_UI
      PhellipeUI.cpp)

target_include_directories(phellipe PUBLIC ".")
