package com.findlk.app

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/** Estado compartilhado entre o serviço (que fala com a tag) e a tela. */
data class TagUiState(
    val protecting: Boolean = false,
    val connected: Boolean = false,
    val tagState: Int? = null,
    val level: Int? = null,
    val rssi: Int? = null,
    val alarm: String? = null,
    val message: String = "",
)

object TagRepository {
    private val _state = MutableStateFlow(TagUiState())
    val state: StateFlow<TagUiState> = _state

    fun update(block: (TagUiState) -> TagUiState) = _state.update(block)
}
