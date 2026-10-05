from machine import Pin, I2C
import ssd1306

i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=400_000)
oled = ssd1306.SSD1306_I2C(128, 64, i2c)

oled.fill(0)
oled.rect(0, 0, 128, 64, 1)
oled.text("Hello", 44, 20)
oled.text("Pico 2 W", 32, 36)
oled.show()
print("hello shown")
