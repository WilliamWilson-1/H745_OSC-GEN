# initUI

`initui-source.jpg` 是用户提供的原始标志，保持原文件不变。字形和文字归原作者所有。

固件不解码 JPEG，也不重新绘制字形。`tools/generate_ui_logo.py` 将原图的深色字形机械转换为紧凑的 2-bit 覆盖字模，输出 `CM7/Core/Inc/ui_logo_data.h`。实际前景色与透明区域背景由当前主题决定，不将白色矩形带入深色界面。

生成好的头文件纳入源码，正常构建不需要 Pillow 或原图处理工具。
