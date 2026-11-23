#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../xiao_pulse_trial/Dashboard.h"

// Records the real renderer's drawing operations and checks the 135x240
// boundary, including text at large distances and every motion state.
class PreviewCanvas {
 public:
  explicit PreviewCanvas(const std::string& path) : out_(path) { assert(out_); }
  void setTextFont(int font) { assert(font == 1); }
  void setTextWrap(bool wrap) { assert(!wrap); }
  void setTextSize(int size) { size_ = size; }
  void setTextColor(uint16_t color) { color_ = color; }
  void setCursor(int x, int y) { x_ = x; y_ = y; }
  void print(const char* value) {
    box(x_, y_, std::string(value).size() * 6 * size_, 8 * size_);
    out_ << "text " << x_ << ' ' << y_ << ' ' << color_ << ' '
         << size_ << ' ' << value << '\n';
  }
  void fillScreen(uint16_t color) { fillRect(0, 0, 135, 240, color); }
  void fillRect(int x, int y, int w, int h, uint16_t color) {
    box(x, y, w, h);
    out_ << "rect " << x << ' ' << y << ' ' << w << ' ' << h << ' ' << color << '\n';
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    box(x, y, w, h);
    out_ << "round " << x << ' ' << y << ' ' << w << ' ' << h << ' ' << r << ' ' << color << '\n';
  }
  void fillCircle(int x, int y, int r, uint16_t color) {
    box(x-r, y-r, r*2+1, r*2+1);
    out_ << "circle " << x << ' ' << y << ' ' << r << ' ' << color << '\n';
  }
  void drawLine(int x, int y, int xx, int yy, uint16_t color) {
    box(x, y, 1, 1); box(xx, yy, 1, 1);
    out_ << "line " << x << ' ' << y << ' ' << xx << ' ' << yy << ' ' << color << '\n';
  }
  void fillTriangle(int x, int y, int xx, int yy, int xxx, int yyy, uint16_t color) {
    box(x,y,1,1); box(xx,yy,1,1); box(xxx,yyy,1,1);
    out_ << "triangle " << x << ' ' << y << ' ' << xx << ' ' << yy << ' ' << xxx << ' ' << yyy << ' ' << color << '\n';
  }
 private:
  void box(int x, int y, int w, int h) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x+w <= 135 && y+h <= 240);
  }
  std::ofstream out_;
  int x_ = 0, y_ = 0, size_ = 1;
  uint16_t color_ = 0;
};

int main(int argc, char** argv) {
  assert(argc == 2);
  namespace fs = std::filesystem;
  const fs::path destination = argv[1];
  fs::create_directories(destination);
  using namespace pulse_trial;
  const std::vector<std::pair<const char*, DashboardData>> samples{
    {"ready", {State::Ready}},
    {"forward", {State::Forward, false, 42.2, 68.4, 26.2, 79.9, 100}},
    {"reverse", {State::Reverse, true, -27.4, 88.6, 116.0, 31.6, 100}},
    {"locked", {State::Disarmed, false, 42.2, 68.4, 26.2, 0, 100}},
    {"wait", {State::Settling, true, 42.2, 68.4, 26.2, 0, 200}},
    {"fault", {State::Fault, false, 42.2, 68.4, 26.2, 0, 100, "STEP TIMER"}},
    {"large", {State::Forward, false, 99999.9, 99999.9, 99999.9, 80, 102400}},
    {"huge", {State::Reverse, true, -1.08e17, 1.08e17, 2.16e17, 5.0e7, 1.126e17}},
    {"edge", {State::Forward, false, 100, 100, 0, 80, 100}},
    {"negative_edge", {State::Reverse, true, -100, 0, 100, 80, 100}},
    {"boot", {State::Boot}}, {"arm", {State::ArmRelease}},
    {"release", {State::Release}}
  };
  for (const auto& sample : samples) {
    PreviewCanvas canvas((destination / (std::string(sample.first) + ".ops")).string());
    dashboard::render(canvas, sample.second);
  }
  puts("PASS: dashboard bounds across all states, signed extrema and large values");
}
