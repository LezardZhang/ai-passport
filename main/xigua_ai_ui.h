#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum { X_AI_READY, X_AI_RECORDING, X_AI_WAITING, X_AI_READING,
               X_AI_ACTIONS, X_AI_ERROR } x_ai_view_t;
typedef enum { X_AI_UP, X_AI_DOWN, X_AI_OK, X_AI_HOLD_OK,
               X_AI_RELEASE_OK, X_AI_BACK } x_ai_input_t;
typedef enum { X_AI_NONE, X_AI_START_VOICE, X_AI_STOP_VOICE, X_AI_HOME } x_ai_effect_t;
typedef struct {
    x_ai_view_t view;
    size_t focus;
    size_t page;
    size_t pages;
    bool has_reply;
} x_ai_ui_t;

static inline size_t x_ai_page_count(size_t height, size_t viewport)
{
    return height && viewport ? 1 + (height - 1) / viewport : 1;
}

static inline void x_ai_complete(x_ai_ui_t *ui, bool success)
{
    ui->view = success ? X_AI_READING : X_AI_ERROR;
    ui->focus = 0;
    if (success) { ui->has_reply = true; ui->page = 0; }
}

static inline x_ai_effect_t x_ai_input(x_ai_ui_t *ui, x_ai_input_t input)
{
    if (input == X_AI_RELEASE_OK && ui->view == X_AI_RECORDING) {
        ui->view = X_AI_WAITING;
        return X_AI_STOP_VOICE;
    }
    if (input == X_AI_BACK) {
        if (ui->view == X_AI_RECORDING) {
            ui->view = X_AI_WAITING;
            return X_AI_STOP_VOICE;
        }
        if (ui->view == X_AI_ACTIONS) ui->view = X_AI_READING;
        else return X_AI_HOME;
        return X_AI_NONE;
    }
    if (ui->view == X_AI_RECORDING || ui->view == X_AI_WAITING) return X_AI_NONE;
    if (ui->view == X_AI_READING) {
        if (input == X_AI_UP && ui->page) --ui->page;
        if (input == X_AI_DOWN && ui->page + 1 < ui->pages) ++ui->page;
        if (input == X_AI_OK) { ui->view = X_AI_ACTIONS; ui->focus = 0; }
    } else if (ui->view == X_AI_READY || ui->view == X_AI_ACTIONS) {
        if (input == X_AI_UP) ui->focus = (ui->focus + 2) % 3;
        if (input == X_AI_DOWN) ui->focus = (ui->focus + 1) % 3;
        if (ui->view == X_AI_READY) {
            if (input == X_AI_HOLD_OK && ui->focus == 0) {
                ui->view = X_AI_RECORDING;
                return X_AI_START_VOICE;
            }
            if (input == X_AI_OK && ui->focus == 1 && ui->has_reply) ui->view = X_AI_READING;
            if (input == X_AI_OK && ui->focus == 2) return X_AI_HOME;
        } else if (input == X_AI_OK) {
            if (ui->focus == 0) ui->view = X_AI_READING;
            if (ui->focus == 1) { ui->view = X_AI_READY; ui->focus = 0; }
            if (ui->focus == 2) return X_AI_HOME;
        }
    } else if (ui->view == X_AI_ERROR && input == X_AI_OK) {
        ui->view = X_AI_READY; ui->focus = 0;
    }
    return X_AI_NONE;
}
