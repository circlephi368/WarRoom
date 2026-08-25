"""生成战略地图管理器应用图标
黄色等腰直角三角形，直角顶点在右下角，透明背景
"""
from PIL import Image, ImageDraw
import os

def draw_triangle(size):
    """在 size x size 的透明画布上绘制黄色等腰直角三角形"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 黄色 RGB(255, 200, 0)
    color = (255, 200, 0, 255)

    # 边距，让三角形不贴边
    margin = max(4, size // 12)
    tri_size = size - margin * 2

    # 直角顶点（右下角方向）
    right_angle = (margin + tri_size, margin + tri_size)
    # 水平直角边左端
    left_bottom = (margin, margin + tri_size)
    # 垂直直角边上端
    right_top = (margin + tri_size, margin)

    # 绘制三角形
    draw.polygon([left_bottom, right_top, right_angle], fill=color)

    return img

def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))

    # 生成主图标 256x256
    icon_256 = draw_triangle(256)
    icon_256.save(os.path.join(out_dir, 'app_icon.png'))

    # 生成多尺寸 PNG
    sizes = [16, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        img = draw_triangle(s)
        images.append(img)
        img.save(os.path.join(out_dir, f'app_icon_{s}.png'))

    # 生成 ICO 文件（包含多尺寸）
    ico_images = [draw_triangle(s) for s in [16, 24, 32, 48, 64, 128, 256]]
    ico_images[0].save(
        os.path.join(out_dir, 'app_icon.ico'),
        format='ICO',
        sizes=[(s, s) for s in [16, 24, 32, 48, 64, 128, 256]]
    )

    print(f"图标已生成到: {out_dir}")
    print("  - app_icon.png  (256x256 透明背景)")
    print("  - app_icon.ico  (多尺寸 Windows 图标)")
    for s in sizes:
        print(f"  - app_icon_{s}.png ({s}x{s})")

if __name__ == '__main__':
    main()
