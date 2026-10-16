#pragma once
#include <windows.h>
#include <functional>
#include <vector>

// Własna animacja minimalizowania i przywracania okien (Minimal i Full).
// Systemową animację min/max Veyra wyłącza na czas działania (SPI_SETANIMATION bez zapisu do profilu — po
// wylogowaniu i tak wraca; przy wyjściu i awarii przywracamy). Zamiast niej kopia okna (miniatura DWM — obraz
// zminimalizowanego okna DWM trzyma, także w Chromium) w przezroczystym oknie „na wierzchu”: przy minimalizowaniu
// wpada do ikony programu w docku, przy przywracaniu z docka wyrasta z niej (dopiero potem okno się pokazuje).
// Cudzego okna nie da się na chwilę ukryć (DWMWA_CLOAK → odmowa dostępu), więc przywracanie spoza Veyry
// (sam program, Alt+Tab) jest bez animacji.
// Wyłącznik: settings.txt windowAnimations=0. Gdy użytkownik ma animacje wyłączone w Windows — też bez animacji.
namespace WindowAnim {
// iconRect: prostokąt ikony okna w docku (piksele ekranu); false = brak ikony / dock schowany → bez animacji
void Install(std::function<bool(HWND, RECT*)> iconRect);
void Uninstall();
void RestoreSystemAnimation();  // filtr awarii
// Przywraca zminimalizowane okna z animacją (wszystkie naraz), potem SW_RESTORE w podanej kolejności;
// activate = ostatnie okno na pierwszy plan. Okno niezminimalizowane / bez animacji — od razu.
void Restore(const std::vector<HWND>& windows, bool activate);
}  // namespace WindowAnim
