#include "gomoku/gui/ui_panel.hpp"
#include <string>

namespace gomoku {
namespace gui {

void UIPanel::init(const sf::Font& font) {
    font_ = &font;
}

void UIPanel::draw_button(sf::RenderWindow& window, const sf::FloatRect& rect,
                           const std::string& text, bool hovered) const {
    sf::RectangleShape btn(sf::Vector2f(rect.width, rect.height));
    btn.setPosition(rect.left, rect.top);
    btn.setFillColor(hovered ? BTN_HOVER : BTN_BG);
    btn.setOutlineColor(sf::Color(120, 120, 130));
    btn.setOutlineThickness(1.0f);
    window.draw(btn);

    sf::Text label;
    label.setFont(*font_);
    label.setString(text);
    label.setCharacterSize(16);
    label.setFillColor(BTN_TEXT);
    sf::FloatRect bounds = label.getLocalBounds();
    label.setPosition(
        rect.left + (rect.width - bounds.width) / 2 - bounds.left,
        rect.top + (rect.height - bounds.height) / 2 - bounds.top
    );
    window.draw(label);
}

// ─── Menu ───────────────────────────────────────────────────────────────────

void UIPanel::draw_menu(sf::RenderWindow& window, sf::Vector2f mouse_pos) const {
    sf::RectangleShape bg(sf::Vector2f(WINDOW_WIDTH, WINDOW_HEIGHT));
    bg.setFillColor(sf::Color(40, 40, 45));
    window.draw(bg);

    // Title
    sf::Text title;
    title.setFont(*font_);
    title.setString("GOMOKU");
    title.setCharacterSize(48);
    title.setFillColor(sf::Color(230, 200, 150));
    title.setStyle(sf::Text::Bold);
    sf::FloatRect tb = title.getLocalBounds();
    title.setPosition((WINDOW_WIDTH - tb.width) / 2 - tb.left, 120);
    window.draw(title);

    // Subtitle
    sf::Text sub;
    sub.setFont(*font_);
    sub.setString("Ninuki-renju");
    sub.setCharacterSize(20);
    sub.setFillColor(sf::Color(180, 180, 180));
    sf::FloatRect sb = sub.getLocalBounds();
    sub.setPosition((WINDOW_WIDTH - sb.width) / 2 - sb.left, 185);
    window.draw(sub);

    // Mode selection buttons
    float btn_w = 300, btn_h = 50;
    float btn_x = (WINDOW_WIDTH - btn_w) / 2;
    float start_y = 280;
    float gap = 20;

    pvb_btn_ = {btn_x, start_y, btn_w, btn_h};
    pvw_btn_ = {btn_x, start_y + btn_h + gap, btn_w, btn_h};
    pvp_btn_ = {btn_x, start_y + 2 * (btn_h + gap), btn_w, btn_h};

    draw_button(window, pvb_btn_, "Play as Black vs AI", pvb_btn_.contains(mouse_pos.x, mouse_pos.y));
    draw_button(window, pvw_btn_, "Play as White vs AI", pvw_btn_.contains(mouse_pos.x, mouse_pos.y));
    draw_button(window, pvp_btn_, "Player vs Player", pvp_btn_.contains(mouse_pos.x, mouse_pos.y));

    // Instructions
    sf::Text info;
    info.setFont(*font_);
    info.setString("ESC quit | U undo | N new | H hint");
    info.setCharacterSize(14);
    info.setFillColor(sf::Color(140, 140, 140));
    sf::FloatRect ib = info.getLocalBounds();
    info.setPosition((WINDOW_WIDTH - ib.width) / 2 - ib.left, WINDOW_HEIGHT - 60);
    window.draw(info);
}

std::optional<GameMode> UIPanel::handle_menu_click(float x, float y) const {
    if (pvb_btn_.contains(x, y)) return GameMode::PvE_Black;
    if (pvw_btn_.contains(x, y)) return GameMode::PvE_White;
    if (pvp_btn_.contains(x, y)) return GameMode::PvP;
    return std::nullopt;
}

// ─── Side panel during play ─────────────────────────────────────────────────

void UIPanel::draw_playing(sf::RenderWindow& window, const Board& board,
                            Stone current_turn, int move_number,
                            std::optional<Pos> last_move,
                            const MoveResult& last_ai_result,
                            bool ai_thinking, uint32_t ai_elapsed_ms,
                            GameMode mode, sf::Vector2f mouse_pos) const {
    // Panel background
    float px = BOARD_PANEL_WIDTH;
    sf::RectangleShape panel(sf::Vector2f(SIDE_PANEL_WIDTH, WINDOW_HEIGHT));
    panel.setPosition(px, 0);
    panel.setFillColor(PANEL_BG);
    window.draw(panel);

    float text_x = px + 20;
    float y = 20;

    // Title
    sf::Text title;
    title.setFont(*font_);
    title.setString("GOMOKU");
    title.setCharacterSize(24);
    title.setFillColor(sf::Color(230, 200, 150));
    title.setStyle(sf::Text::Bold);
    title.setPosition(text_x, y);
    window.draw(title);
    y += 45;

    // Mode
    sf::Text mode_text;
    mode_text.setFont(*font_);
    std::string mode_str;
    switch (mode) {
        case GameMode::PvE_Black: mode_str = "Human (Black) vs AI"; break;
        case GameMode::PvE_White: mode_str = "AI vs Human (White)"; break;
        case GameMode::PvP:       mode_str = "Player vs Player";    break;
    }
    mode_text.setString(mode_str);
    mode_text.setCharacterSize(14);
    mode_text.setFillColor(sf::Color(180, 180, 180));
    mode_text.setPosition(text_x, y);
    window.draw(mode_text);
    y += 35;

    // Separator
    sf::RectangleShape sep(sf::Vector2f(SIDE_PANEL_WIDTH - 40, 1));
    sep.setFillColor(sf::Color(80, 80, 85));
    sep.setPosition(text_x, y);
    window.draw(sep);
    y += 15;

    // Turn + move number
    sf::Text turn_text;
    turn_text.setFont(*font_);
    std::string turn_str = (current_turn == Stone::Black) ? "Black" : "White";
    turn_text.setString("Turn: " + turn_str + "  (move #" + std::to_string(move_number) + ")");
    turn_text.setCharacterSize(16);
    turn_text.setFillColor(TEXT_COLOR);
    turn_text.setPosition(text_x, y);
    window.draw(turn_text);
    y += 30;

    // Turn stone indicator
    sf::CircleShape turn_stone(8);
    turn_stone.setOrigin(8, 8);
    turn_stone.setPosition(text_x + 8, y + 4);
    turn_stone.setFillColor(current_turn == Stone::Black ? BLACK_STONE_COLOR : WHITE_STONE_COLOR);
    if (current_turn == Stone::White) {
        turn_stone.setOutlineColor(sf::Color(150, 150, 150));
        turn_stone.setOutlineThickness(1.0f);
    }
    window.draw(turn_stone);

    if (ai_thinking) {
        sf::Text thinking;
        thinking.setFont(*font_);
        std::string timer_str = "AI thinking... "
            + std::to_string(ai_elapsed_ms) + " ms";
        thinking.setString(timer_str);
        thinking.setCharacterSize(14);
        thinking.setFillColor(sf::Color(100, 200, 255));
        thinking.setPosition(text_x + 24, y - 2);
        window.draw(thinking);
    }
    y += 30;

    // Captures
    sf::Text cap_title;
    cap_title.setFont(*font_);
    cap_title.setString("Captures");
    cap_title.setCharacterSize(16);
    cap_title.setFillColor(TEXT_COLOR);
    cap_title.setStyle(sf::Text::Bold);
    cap_title.setPosition(text_x, y);
    window.draw(cap_title);
    y += 25;

    auto draw_line = [&](const std::string& s, int size = 14) {
        sf::Text t;
        t.setFont(*font_);
        t.setString(s);
        t.setCharacterSize(size);
        t.setFillColor(sf::Color(200, 200, 200));
        t.setPosition(text_x + 10, y);
        window.draw(t);
        y += static_cast<float>(size) + 6;
    };

    draw_line("Black: " + std::to_string(board.black_captures) + " / 5 pairs");
    draw_line("White: " + std::to_string(board.white_captures) + " / 5 pairs");
    y += 8;

    // Last move
    if (last_move.has_value()) {
        draw_line("Last: " + pos_to_notation(*last_move));
        y += 2;
    }

    // AI search stats
    if (!last_ai_result.best_move.is_sentinel()) {
        sep.setPosition(text_x, y);
        window.draw(sep);
        y += 15;

        sf::Text ai_title;
        ai_title.setFont(*font_);
        ai_title.setString("AI Search Info");
        ai_title.setCharacterSize(16);
        ai_title.setFillColor(TEXT_COLOR);
        ai_title.setStyle(sf::Text::Bold);
        ai_title.setPosition(text_x, y);
        window.draw(ai_title);
        y += 25;

        auto& r = last_ai_result;
        std::string type_str;
        switch (r.search_type) {
            case SearchType::ImmediateWin: type_str = "Immediate Win"; break;
            case SearchType::VCF:          type_str = "VCF";           break;
            case SearchType::VCT:          type_str = "VCT";           break;
            case SearchType::Defense:      type_str = "Defense";       break;
            case SearchType::AlphaBeta:    type_str = "Alpha-Beta";    break;
        }

        auto draw_stat = [&](const std::string& line) {
            sf::Text t;
            t.setFont(*font_);
            t.setString(line);
            t.setCharacterSize(13);
            t.setFillColor(sf::Color(180, 180, 180));
            t.setPosition(text_x + 10, y);
            window.draw(t);
            y += 20;
        };

        draw_stat("Type: " + type_str);
        draw_stat("Score: " + std::to_string(r.score));
        draw_stat("Depth: " + std::to_string(r.depth));
        draw_stat("Nodes: " + std::to_string(r.nodes));
        draw_stat("Time: " + std::to_string(r.time_ms) + " ms");
        if (r.nps > 0) draw_stat("Speed: " + std::to_string(r.nps) + " kN/s");
        if (r.tt_usage > 0) draw_stat("TT: " + std::to_string(r.tt_usage) + "%");
    }

    // Buttons at bottom
    float btn_w = SIDE_PANEL_WIDTH - 40;
    float btn_h = 36;
    float btn_x = px + 20;

    hint_btn_     = {btn_x, static_cast<float>(WINDOW_HEIGHT - 180), btn_w, btn_h};
    undo_btn_     = {btn_x, static_cast<float>(WINDOW_HEIGHT - 130), btn_w, btn_h};
    new_game_btn_ = {btn_x, static_cast<float>(WINDOW_HEIGHT - 80),  btn_w, btn_h};

    draw_button(window, hint_btn_, "Hint (H)", hint_btn_.contains(mouse_pos.x, mouse_pos.y));
    draw_button(window, undo_btn_, "Undo (U)", undo_btn_.contains(mouse_pos.x, mouse_pos.y));
    draw_button(window, new_game_btn_, "Menu (ESC)", new_game_btn_.contains(mouse_pos.x, mouse_pos.y));
}

// ─── Game over overlay ──────────────────────────────────────────────────────

void UIPanel::draw_game_over(sf::RenderWindow& window, Stone winner,
                              const Board& board) const {
    // Semi-transparent overlay on board area
    sf::RectangleShape overlay(sf::Vector2f(BOARD_PANEL_WIDTH, WINDOW_HEIGHT));
    overlay.setFillColor(sf::Color(0, 0, 0, 120));
    window.draw(overlay);

    // Banner
    sf::RectangleShape banner(sf::Vector2f(400, 120));
    banner.setOrigin(200, 60);
    banner.setPosition(BOARD_PANEL_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f);
    banner.setFillColor(sf::Color(40, 40, 45, 230));
    banner.setOutlineColor(sf::Color(230, 200, 150));
    banner.setOutlineThickness(2.0f);
    window.draw(banner);

    std::string winner_str = (winner == Stone::Black) ? "BLACK" : "WHITE";
    sf::Text text;
    text.setFont(*font_);
    text.setString(winner_str + " WINS!");
    text.setCharacterSize(32);
    text.setFillColor(sf::Color(230, 200, 150));
    text.setStyle(sf::Text::Bold);
    sf::FloatRect tb = text.getLocalBounds();
    text.setPosition(BOARD_PANEL_WIDTH / 2.0f - tb.width / 2 - tb.left,
                     WINDOW_HEIGHT / 2.0f - 35);
    window.draw(text);

    std::string win_type = (board.captures(winner) >= 5)
        ? "by 10 captures (5 pairs)"
        : "by 5-in-a-row";
    sf::Text sub;
    sub.setFont(*font_);
    sub.setString(win_type);
    sub.setCharacterSize(16);
    sub.setFillColor(sf::Color(200, 200, 200));
    sf::FloatRect sb = sub.getLocalBounds();
    sub.setPosition(BOARD_PANEL_WIDTH / 2.0f - sb.width / 2 - sb.left,
                    WINDOW_HEIGHT / 2.0f + 10);
    window.draw(sub);
}

bool UIPanel::is_hint_clicked(float x, float y) const {
    return hint_btn_.contains(x, y);
}

bool UIPanel::is_undo_clicked(float x, float y) const {
    return undo_btn_.contains(x, y);
}

bool UIPanel::is_new_game_clicked(float x, float y) const {
    return new_game_btn_.contains(x, y);
}

} // namespace gui
} // namespace gomoku
