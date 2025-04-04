#include "Game.h"
#include "Piece.h"
#include "Team.h"
#include "TextureManager.h"
#include <cmath>  
#include <iostream>
#include "raylib.h"
using namespace std;

// Add static member definitions
const Color Game::LIGHT_SQUARE = RAYWHITE;
const Color Game::DARK_SQUARE = DARKGRAY;
const Color Game::MOVE_HIGHLIGHT = GREEN;

Sound moveSound;
Sound captureSound;
Sound checkSound;
Sound promotionSound;
Texture2D backgroundTexture;

float currentRotation = 0.0f;
float targetRotation = 0.0f;
const float rotationSpeed = 2.0f;  // Adjust this value to control speed

// Use the Move struct from Move.h
Move lastMove;

Vector2 promotionSquare = {-1, -1};

Game::Game() : 
    whiteTeam(true),
    blackTeam(false),
    selectedPiece(nullptr),
    isWhiteTurn(true),
    boardRotated(false),
    currentState(MENU),  // Start in MENU state
    promotionSquare({-1, -1})
{
    // Set window to be maximized by default
    SetConfigFlags(FLAG_WINDOW_MAXIMIZED);

    // Initialize OpenGL context and wait for it to be ready
    while (!IsWindowReady()) { }

    // Initialize audio device
    InitAudioDevice();

    // Load move sound
    moveSound = LoadSound("assets/move.mp3");

    // Load capture sound
    captureSound = LoadSound("assets/capture.mp3");

    // Load check sound
    checkSound = LoadSound("assets/check.mp3");

    // Load promotion sound
    promotionSound = LoadSound("assets/promote.mp3");

    // Load background texture
    backgroundTexture = LoadTexture("assets/background.jpg");

    // Initialize texture manager and ensure it's ready
    auto texManager = TextureManager::GetInstance();
    texManager->Initialize();

    // Load piece textures
    const std::string textureBasePath = "assets/";
    const std::vector<std::string> pieceTypes = {"pawn", "rook", "knight", "bishop", "queen", "king"};
    const std::vector<std::string> colors = {"white", "black"};

    for (const auto& color : colors) {
        for (const auto& piece : pieceTypes) {
            std::string key = color + "_" + piece;
            std::string path = textureBasePath + key + ".png";
            texManager->LoadTextureFromFile(key, path);
        }
    }
}

Game::~Game() {
    // Unload move sound
    UnloadSound(moveSound);

    // Unload capture sound
    UnloadSound(captureSound);

    // Unload check sound
    UnloadSound(checkSound);

    // Unload promotion sound
    UnloadSound(promotionSound);

    // Unload background texture
    UnloadTexture(backgroundTexture);

    // Close audio device
    CloseAudioDevice();

    // Cleanup textures first
    TextureManager::GetInstance()->UnloadAllTextures();
    TextureManager::Cleanup();
    
    CloseWindow();
}

void Game::Run() {
    while (!WindowShouldClose()) {
        HandleInput();

        // Smooth rotation logic
        if (currentRotation != targetRotation) {
            if (currentRotation < targetRotation) {
                currentRotation += rotationSpeed;
                if (currentRotation > targetRotation) currentRotation = targetRotation;
            } else {
                currentRotation -= rotationSpeed;
                if (currentRotation < targetRotation) currentRotation = targetRotation;
            }
        }

        BeginDrawing();
        {
            ClearBackground(RAYWHITE);
            
            switch (GetGameState()) {
                case MENU:
                    DrawMenu();
                    break;
                case PLAY:
                    DrawBoard();
                    DrawLabels();
                    Draw();
                    // Check for checkmate
                    if (IsCheckmate(isWhiteTurn)) {
                        SetGameState(CHECKMATE);
                    }
                    break;
                case PROMOTION:
                    DrawBoard();
                    DrawLabels();
                    Draw();
                    DrawPromotionUI();
                    break;
                case CHECKMATE:
                    DrawBoard();
                    DrawLabels();
                    Draw();
                    DrawCheckmateMenu();
                    break;
            }
        }
        EndDrawing();
    }
}

void Game::Draw() {
    // Get window dimensions for centering
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    
    // Calculate board offset
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (windowWidth - boardPixelSize) / 2;
    int offsetY = (windowHeight - boardPixelSize) / 2;

    // Draw the board first
    DrawBoard();
    
    // Draw labels
    DrawLabels();

    // Only draw move highlights if we're in PLAY state
    if (GetGameState() == PLAY) {
        // Draw capture highlights first
        if (selectedPiece) {
            for (const auto& move : validMoves) {
                int drawX = boardRotated ? BOARD_SIZE - 1 - move.x : move.x;
                int drawY = boardRotated ? BOARD_SIZE - 1 - move.y : move.y;
                if (GetPieceAt(move.x, move.y) && GetPieceAt(move.x, move.y)->GetType() != PieceType::KING) {
                    // Highlight capture moves with a rectangle
                    DrawRectangle(
                        offsetX + drawX * TILE_SIZE,
                        offsetY + drawY * TILE_SIZE,
                        TILE_SIZE,
                        TILE_SIZE,
                        RED
                    );
                } else if (selectedPiece->GetType() == PieceType::PAWN &&
                           abs(lastMove.end.y - lastMove.start.y) == 2 &&
                           lastMove.piece->GetType() == PieceType::PAWN &&
                           lastMove.end.x == move.x &&
                           lastMove.end.y == selectedPiece->GetY() &&  // Check if enemy pawn is on same rank
                           abs(move.x - selectedPiece->GetX()) == 1 && // Check if moving diagonally
                           move.y == lastMove.end.y + (selectedPiece->IsWhite() ? -1 : 1)) { // Check if moving to correct square
                    // Highlight en passant moves with a blue rectangle
                    DrawRectangle(
                        offsetX + drawX * TILE_SIZE,
                        offsetY + drawY * TILE_SIZE,
                        TILE_SIZE,
                        TILE_SIZE,
                        BLUE
                    );
                }
            }
        }
    }

    // Find kings and check if they're in check
    const Piece* whiteKing = nullptr;
    const Piece* blackKing = nullptr;
    for (const auto& piece : whiteTeam.GetPieces()) {
        if (piece->GetType() == PieceType::KING) {
            whiteKing = piece.get();
            break;
        }
    }
    for (const auto& piece : blackTeam.GetPieces()) {
        if (piece->GetType() == PieceType::KING) {
            blackKing = piece.get();
            break;
        }
    }

    // Draw check highlight
    if (whiteKing && IsSquareUnderAttack(whiteKing->GetX(), whiteKing->GetY(), false)) {
        int drawX = boardRotated ? BOARD_SIZE - 1 - whiteKing->GetX() : whiteKing->GetX();
        int drawY = boardRotated ? BOARD_SIZE - 1 - whiteKing->GetY() : whiteKing->GetY();
        DrawRectangle(
            offsetX + drawX * TILE_SIZE,
            offsetY + drawY * TILE_SIZE,
            TILE_SIZE,
            TILE_SIZE,
            Color{255, 0, 0, 100} // Semi-transparent red
        );
    }
    if (blackKing && IsSquareUnderAttack(blackKing->GetX(), blackKing->GetY(), true)) {
        int drawX = boardRotated ? BOARD_SIZE - 1 - blackKing->GetX() : blackKing->GetX();
        int drawY = boardRotated ? BOARD_SIZE - 1 - blackKing->GetY() : blackKing->GetY();
        DrawRectangle(
            offsetX + drawX * TILE_SIZE,
            offsetY + drawY * TILE_SIZE,
            TILE_SIZE,
            TILE_SIZE,
            Color{255, 0, 0, 100} // Semi-transparent red
        );
    }

    // Draw pieces
    for (const auto& piece : whiteTeam.GetPieces()) {
        Vector2 pos = GetCenteredPiecePosition(*piece);
        Texture2D tex = piece->GetTexture();
        if (tex.id > 0) {  // Only draw if texture is valid
            DrawTexture(tex, pos.x, pos.y, WHITE);
        } else {
            // Draw a placeholder if texture is missing
            DrawRectangle(pos.x, pos.y, TILE_SIZE/2, TILE_SIZE/2, WHITE);
        }
    }
    
    for (const auto& piece : blackTeam.GetPieces()) {
        Vector2 pos = GetCenteredPiecePosition(*piece);
        Texture2D tex = piece->GetTexture();
        if (tex.id > 0) {  // Only draw if texture is valid
            DrawTexture(tex, pos.x, pos.y, WHITE);
        } else {
            // Draw a placeholder if texture is missing
            DrawRectangle(pos.x, pos.y, TILE_SIZE/2, TILE_SIZE/2, BLACK);
        }
    }

    // Draw available move highlights only in PLAY state
    if (GetGameState() == PLAY && selectedPiece) {
        for (const auto& move : validMoves) {
            int drawX = boardRotated ? BOARD_SIZE - 1 - move.x : move.x;
            int drawY = boardRotated ? BOARD_SIZE - 1 - move.y : move.y;
            if (!GetPieceAt(move.x, move.y) && 
                !(selectedPiece->GetType() == PieceType::PAWN &&
                  abs(lastMove.end.y - lastMove.start.y) == 2 &&
                  lastMove.piece->GetType() == PieceType::PAWN &&
                  lastMove.end.x == move.x &&
                  lastMove.end.y == selectedPiece->GetY() &&
                  abs(move.x - selectedPiece->GetX()) == 1 &&
                  move.y == lastMove.end.y + (selectedPiece->IsWhite() ? -1 : 1))) {
                // Highlight available moves with a circle
                DrawCircle(
                    offsetX + (drawX * TILE_SIZE) + TILE_SIZE/2,
                    offsetY + (drawY * TILE_SIZE) + TILE_SIZE/2,
                    10,
                    MOVE_HIGHLIGHT
                );
            }
        }
    }

    // Draw promotion UI if in promotion state
    if (GetGameState() == PROMOTION) {
        DrawPromotionUI();
    }
}

void Game::HandleInput() {
    if (GetGameState() == MENU) {
        // Check for Play button click
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            int playWidth = MeasureText("Play", 30);
            int buttonWidth = playWidth + 60;
            int buttonHeight = 40;
            int buttonX = (GetScreenWidth() - buttonWidth) / 2;
            int buttonY = GetScreenHeight() / 2;

            if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
                mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
                SetGameState(PLAY);
            }
        }
        return;
    }

    if (GetGameState() == CHECKMATE) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            int windowWidth = GetScreenWidth();
            int windowHeight = GetScreenHeight();

            // Check Play Again button
            const char* playAgainText = "Play Again";
            int playAgainWidth = MeasureText(playAgainText, 30);
            int buttonWidth = playAgainWidth + 60;
            int buttonHeight = 40;
            int buttonX = (windowWidth - buttonWidth) / 2;
            int buttonY = windowHeight / 2;

            if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
                mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
                // Reset the game
                whiteTeam.Reset();
                blackTeam.Reset();
                isWhiteTurn = true;
                boardRotated = false;
                selectedPiece = nullptr;
                validMoves.clear();
                SetGameState(PLAY);
                return;
            }

            // Check Quit button
            const char* quitText = "Quit";
            int quitWidth = MeasureText(quitText, 30);
            buttonWidth = quitWidth + 60;
            buttonY += buttonHeight + 20;
            buttonX = (windowWidth - buttonWidth) / 2;

            if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
                mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
                // Clean up and exit
                UnloadSound(moveSound);
                UnloadSound(captureSound);
                UnloadSound(checkSound);
                UnloadSound(promotionSound);
                UnloadTexture(backgroundTexture);
                CloseWindow();
                exit(0);  // Force exit the program
                return;
            }
        }
        return;
    }

    if (GetGameState() == PROMOTION) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            
            // Calculate board position
            int boardPixelSize = TILE_SIZE * BOARD_SIZE;
            int offsetX = (GetScreenWidth() - boardPixelSize) / 2;
            int offsetY = (GetScreenHeight() - boardPixelSize) / 2;

            float spacing = 100.0f;
            float startX = offsetX + (boardPixelSize - spacing * 4) / 2;
            float y = offsetY + (boardPixelSize - 64) / 2;  // 64 is piece texture size

            // Check which piece was clicked
            for (int i = 0; i < 4; i++) {
                Rectangle pieceRect = {
                    startX + i * spacing,
                    y,
                    64,  // Piece texture size
                    64   // Piece texture size
                };

                if (CheckCollisionPointRec(mousePos, pieceRect)) {
                    PieceType promotionType;
                    switch (i) {
                        case 0: promotionType = PieceType::QUEEN; break;
                        case 1: promotionType = PieceType::ROOK; break;
                        case 2: promotionType = PieceType::BISHOP; break;
                        case 3: promotionType = PieceType::KNIGHT; break;
                        default: return;
                    }
                    PromotePawn(promotionType);
                    SetGameState(PLAY);
                    return;
                }
            }
        }
        return;
    }

    if (GetGameState() == PLAY) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mousePos = GetMousePosition();
            Vector2 boardPos = ScreenToBoard(mousePos);
            
            if (boardPos.x >= 0 && boardPos.x < BOARD_SIZE &&
                boardPos.y >= 0 && boardPos.y < BOARD_SIZE) {
                
                // Get the clicked piece
                Piece* clickedPiece = const_cast<Piece*>(GetPieceAt(boardPos.x, boardPos.y));
                
                if (selectedPiece) {
                    // If clicking a piece of the current player's color
                    if (clickedPiece && clickedPiece->IsWhite() == isWhiteTurn) {
                        // Select the new piece
                        selectedPiece = clickedPiece;
                        validMoves = GetValidMoves(selectedPiece);
                    } else {
                        // Try to move to the clicked square
                        bool isValidMove = false;
                        for (const auto& move : validMoves) {
                            if (move.x == boardPos.x && move.y == boardPos.y) {
                                isValidMove = true;
                                break;
                            }
                        }
                        
                        if (isValidMove) {
                            MovePiece(boardPos.x, boardPos.y);
                        } else {
                            // Deselect if clicking an invalid move
                            selectedPiece = nullptr;
                            validMoves.clear();
                        }
                    }
                } else if (clickedPiece && clickedPiece->IsWhite() == isWhiteTurn) {
                    // Select new piece if it's the current player's turn
                    selectedPiece = clickedPiece;
                    validMoves = GetValidMoves(selectedPiece);
                }
            }
        }
    }
}

void Game::MovePiece(int x, int y) {
    if (!selectedPiece) return;

    // Check if the move is valid
    Vector2 targetPos = {(float)x, (float)y};
    bool isValidMove = false;
    for (const auto& move : validMoves) {
        if (move.x == targetPos.x && move.y == targetPos.y) {
            isValidMove = true;
            break;
        }
    }

    if (isValidMove) {
        // Check for en passant
        if (selectedPiece->GetType() == PieceType::PAWN &&
            abs(lastMove.end.y - lastMove.start.y) == 2 &&
            lastMove.piece->GetType() == PieceType::PAWN &&
            lastMove.end.x == x &&
            lastMove.end.y == selectedPiece->GetY() &&  // Check if enemy pawn is on same rank
            abs(x - selectedPiece->GetX()) == 1 &&      // Check if moving diagonally
            y == lastMove.end.y + (selectedPiece->IsWhite() ? -1 : 1)) {  // Check if moving to correct square behind enemy pawn

            // En passant capture
            Piece* capturedPawn = const_cast<Piece*>(GetPieceAt(x, lastMove.end.y));
            if (capturedPawn) {
                if (capturedPawn->IsWhite()) {
                    whiteTeam.RemovePieceAt(x, lastMove.end.y);
                } else {
                    blackTeam.RemovePieceAt(x, lastMove.end.y);
                }
            }

            // Play capture sound for en passant
            if (captureSound.stream.buffer != NULL) {
                PlaySound(captureSound);
            }
        }

        // Remove the captured piece if present
        Piece* targetPiece = const_cast<Piece*>(GetPieceAt(x, y));
        if (targetPiece && targetPiece->IsWhite() != selectedPiece->IsWhite()) {
            if (targetPiece->GetType() == PieceType::KING) {
                // Cannot capture the king, invalidate the move
                return;
            }
            if (targetPiece->IsWhite()) {
                whiteTeam.RemovePieceAt(x, y);
            } else {
                blackTeam.RemovePieceAt(x, y);
            }
            // Play capture sound
            if (captureSound.stream.buffer != NULL) {
                PlaySound(captureSound);
            }
        } else {
            // Play move sound
            if (moveSound.stream.buffer != NULL) {
                PlaySound(moveSound);
            }
        }

        // Update last move
        lastMove = {selectedPiece->GetPosition(), targetPos, selectedPiece};

        selectedPiece->SetPosition(x, y);

        // Check for pawn promotion
        if (selectedPiece->GetType() == PieceType::PAWN) {
            int promotionRank = selectedPiece->IsWhite() ? 0 : 7;
            if (y == promotionRank) {
                promotionSquare = {(float)x, (float)y};
                SetGameState(PROMOTION);
                // Don't switch turns yet - wait for promotion to complete
                // Play promotion sound
                if (promotionSound.stream.buffer != NULL) {
                    PlaySound(promotionSound);
                }
                return;
            }
        }

        // Check if this move puts the opponent's king in check
        const Team& opposingTeam = isWhiteTurn ? blackTeam : whiteTeam;
        const Piece* opposingKing = nullptr;
        for (const auto& piece : opposingTeam.GetPieces()) {
            if (piece->GetType() == PieceType::KING) {
                opposingKing = piece.get();
                break;
            }
        }
        
        if (opposingKing && IsSquareUnderAttack(opposingKing->GetX(), opposingKing->GetY(), isWhiteTurn)) {
            // Play check sound
            if (checkSound.stream.buffer != NULL) {
                PlaySound(checkSound);
            }
        }

        // Only switch turns if not promoting
        if (GetGameState() != PROMOTION) {
            isWhiteTurn = !isWhiteTurn;
            boardRotated = !boardRotated;
        }
    }

    // Clear selection
    selectedPiece = nullptr;
    validMoves.clear();
}

void Game::PromotePawn(PieceType type) {
    // Create new piece at promotion square
    Team& team = isWhiteTurn ? whiteTeam : blackTeam;
    team.RemovePieceAt(promotionSquare.x, promotionSquare.y);
    team.AddPiece(type, promotionSquare.x, promotionSquare.y);

    // Play move sound
    if (moveSound.stream.buffer != NULL) {
        PlaySound(moveSound);
    }

    // Clear any selected piece and valid moves
    selectedPiece = nullptr;
    validMoves.clear();

    // Check for checkmate on the opponent's side before switching turns
    bool opponentInCheckmate = IsCheckmate(!isWhiteTurn);

    // Switch turns and rotate board after promotion is complete
    isWhiteTurn = !isWhiteTurn;
    boardRotated = !boardRotated;
    
    // Reset promotion square
    promotionSquare = {-1, -1};

    // If opponent was in checkmate, show the menu
    if (opponentInCheckmate) {
        SetGameState(CHECKMATE);
    } else {
        SetGameState(PLAY);
    }
}

void Game::DrawPromotionUI() {
    // Draw semi-transparent background
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 200});

    // Get texture manager instance
    auto texManager = TextureManager::GetInstance();
    
    // The pawn being promoted belongs to the current player
    bool isWhitePiece = isWhiteTurn;

    // Calculate board position
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (GetScreenWidth() - boardPixelSize) / 2;
    int offsetY = (GetScreenHeight() - boardPixelSize) / 2;

    // Draw promotion pieces
    const std::vector<std::string> pieceTypes = {"queen", "rook", "bishop", "knight"};
    float spacing = 100.0f;
    float startX = offsetX + (boardPixelSize - spacing * pieceTypes.size()) / 2;
    float y = offsetY + (boardPixelSize - 64) / 2;  // 64 is piece texture size

    for (size_t i = 0; i < pieceTypes.size(); i++) {
        Vector2 piecePos = {startX + i * spacing, y};
        std::string prefix = isWhitePiece ? "white_" : "black_";
        Texture2D texture = texManager->GetTexture(prefix + pieceTypes[i]);
        
        DrawTexture(texture, piecePos.x, piecePos.y, WHITE);  // Always use WHITE tint to preserve piece color
    }
}

void Game::DrawLabels() {
    // Get window dimensions for centering
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    
    // Calculate board offset
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (windowWidth - boardPixelSize) / 2;
    int offsetY = (windowHeight - boardPixelSize) / 2;

    // Constants for label styling
    const int LABEL_SIZE = 20;
    const int LABEL_MARGIN = 8;
    const Color LABEL_COLOR = RAYWHITE;
    const Color SHADOW_COLOR = BLACK;
    const int SHADOW_OFFSET = 1;
    const int PLAYER_LABEL_SIZE = 25;  // Slightly larger font for player labels

    // Draw player labels
    // Player 1 (top left)
    DrawText(  // Shadow
        "Player 1",
        LABEL_MARGIN + SHADOW_OFFSET,
        LABEL_MARGIN + SHADOW_OFFSET,
        PLAYER_LABEL_SIZE,
        SHADOW_COLOR
    );
    DrawText(  // Text
        "Player 1",
        LABEL_MARGIN,
        LABEL_MARGIN,
        PLAYER_LABEL_SIZE,
        LABEL_COLOR
    );

    // Player 2 (bottom left)
    DrawText(  // Shadow
        "Player 2",
        LABEL_MARGIN + SHADOW_OFFSET,
        windowHeight - PLAYER_LABEL_SIZE - LABEL_MARGIN + SHADOW_OFFSET,
        PLAYER_LABEL_SIZE,
        SHADOW_COLOR
    );
    DrawText(  // Text
        "Player 2",
        LABEL_MARGIN,
        windowHeight - PLAYER_LABEL_SIZE - LABEL_MARGIN,
        PLAYER_LABEL_SIZE,
        LABEL_COLOR
    );

    // Draw vertical labels (1 to 8)
    for (int y = 0; y < BOARD_SIZE; y++) {
        // Keep the actual square notation (1-8 from bottom to top)
        char rankLabel = boardRotated ? ('1' + (BOARD_SIZE - 1 - y)) : ('1' + y);
        char label[2] = {rankLabel, '\0'};
        
        // Draw numbers outside the board area with shadow
        DrawText(  // Draw shadow first
            label,
            offsetX - LABEL_SIZE - LABEL_MARGIN * 3 + SHADOW_OFFSET,
            offsetY + y * TILE_SIZE + (TILE_SIZE - LABEL_SIZE) / 2 + SHADOW_OFFSET,
            LABEL_SIZE,
            SHADOW_COLOR
        );
        DrawText(  // Draw main text
            label,
            offsetX - LABEL_SIZE - LABEL_MARGIN * 3,
            offsetY + y * TILE_SIZE + (TILE_SIZE - LABEL_SIZE) / 2,
            LABEL_SIZE,
            LABEL_COLOR
        );
    }

    // Draw horizontal labels (a to h)
    for (int x = 0; x < BOARD_SIZE; x++) {
        // Use lowercase letters (a-h from left to right)
        char colLabel = 'a' + x;  
        char label[2] = {colLabel, '\0'};
        
        // Draw below the board with shadow effect
        DrawText(  // Draw shadow first
            label,
            offsetX + x * TILE_SIZE + (TILE_SIZE - LABEL_SIZE) / 2 + SHADOW_OFFSET,
            offsetY + boardPixelSize + LABEL_MARGIN + SHADOW_OFFSET,
            LABEL_SIZE,
            SHADOW_COLOR
        );
        DrawText(  // Draw main text
            label,
            offsetX + x * TILE_SIZE + (TILE_SIZE - LABEL_SIZE) / 2,
            offsetY + boardPixelSize + LABEL_MARGIN,
            LABEL_SIZE,
            LABEL_COLOR
        );
    }
}

void Game::DrawBoard() {
    // Get window dimensions for centering
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    
    // Calculate board offset
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (windowWidth - boardPixelSize) / 2;
    int offsetY = (windowHeight - boardPixelSize) / 2;

    // Draw background to cover entire screen
    Rectangle sourceRect = { 0, 0, (float)backgroundTexture.width, (float)backgroundTexture.height };
    Rectangle destRect = { 0, 0, (float)windowWidth, (float)windowHeight };
    DrawTexturePro(backgroundTexture, sourceRect, destRect, {0, 0}, 0.0f, WHITE);

    // Draw the chess board squares
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int drawX = boardRotated ? BOARD_SIZE - 1 - x : x;
            int drawY = boardRotated ? BOARD_SIZE - 1 - y : y;
            Color squareColor = ((x + y) % 2 == 0) ? LIGHT_SQUARE : DARK_SQUARE;
            DrawRectangle(
                offsetX + drawX * TILE_SIZE,
                offsetY + drawY * TILE_SIZE,
                TILE_SIZE,
                TILE_SIZE,
                squareColor
            );
        }
    }

    // Draw pieces
    for (const auto& piece : whiteTeam.GetPieces()) {
        Vector2 pos = GetCenteredPiecePosition(*piece);
        Texture2D tex = piece->GetTexture();
        if (tex.id > 0) {  // Only draw if texture is valid
            DrawTexture(tex, pos.x, pos.y, WHITE);
        } else {
            // Draw a placeholder if texture is missing
            DrawRectangle(pos.x, pos.y, TILE_SIZE/2, TILE_SIZE/2, WHITE);
        }
    }
    
    for (const auto& piece : blackTeam.GetPieces()) {
        Vector2 pos = GetCenteredPiecePosition(*piece);
        Texture2D tex = piece->GetTexture();
        if (tex.id > 0) {  // Only draw if texture is valid
            DrawTexture(tex, pos.x, pos.y, WHITE);
        } else {
            // Draw a placeholder if texture is missing
            DrawRectangle(pos.x, pos.y, TILE_SIZE/2, TILE_SIZE/2, BLACK);
        }
    }
}

void Game::DrawMenu() {
    // Draw background
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), DARKBLUE);

    const char* title = "Chess Game";
    const char* developers = "Developed by: Shehryar, Sufyan & Faizan";
    const char* playText = "Play";

    int titleWidth = MeasureText(title, 40);
    int devWidth = MeasureText(developers, 20);
    int playWidth = MeasureText(playText, 30);

    // Draw title
    DrawText(title, (GetScreenWidth() - titleWidth) / 2, GetScreenHeight() / 3, 40, RAYWHITE);

    // Draw Play button
    int buttonWidth = playWidth + 60;  // Add padding
    int buttonHeight = 40;
    int buttonX = (GetScreenWidth() - buttonWidth) / 2;
    int buttonY = GetScreenHeight() / 2;
    
    // Draw button background with hover effect
    Vector2 mousePos = GetMousePosition();
    Color buttonColor = RAYWHITE;
    if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
        mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
        buttonColor = LIGHTGRAY;  // Change color on hover
    }
    DrawRectangle(buttonX, buttonY, buttonWidth, buttonHeight, buttonColor);
    
    // Draw button text
    DrawText(playText, 
        buttonX + (buttonWidth - playWidth) / 2,
        buttonY + (buttonHeight - 30) / 2,  // 30 is font size
        30, 
        BLACK
    );

    // Draw developers credit
    DrawText(developers, 
        (GetScreenWidth() - devWidth) / 2,
        GetScreenHeight() - 50,
        20,
        LIGHTGRAY
    );
}

Vector2 Game::ScreenToBoard(Vector2 screenPos) {
    // Calculate board offset
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (GetScreenWidth() - boardPixelSize) / 2;
    int offsetY = (GetScreenHeight() - boardPixelSize) / 2;

    // Adjust screen position by offset
    float relativeX = screenPos.x - offsetX;
    float relativeY = screenPos.y - offsetY;

    // Convert to board coordinates
    int boardX = (int)(relativeX / TILE_SIZE);
    int boardY = (int)(relativeY / TILE_SIZE);

    // Handle board rotation
    if (boardRotated) {
        boardX = BOARD_SIZE - 1 - boardX;
        boardY = BOARD_SIZE - 1 - boardY;
    }

    return Vector2{(float)boardX, (float)boardY};
}

Vector2 Game::BoardToScreen(int x, int y) {
    // Get window dimensions for centering
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    
    // Calculate board offset
    int boardPixelSize = TILE_SIZE * BOARD_SIZE;
    int offsetX = (windowWidth - boardPixelSize) / 2;
    int offsetY = (windowHeight - boardPixelSize) / 2;

    // Handle board rotation
    if (boardRotated) {
        x = BOARD_SIZE - 1 - x;
        y = BOARD_SIZE - 1 - y;
    }

    return (Vector2){
        static_cast<float>(offsetX + x * TILE_SIZE),
        static_cast<float>(offsetY + y * TILE_SIZE)
    };
}

Vector2 Game::GetCenteredPiecePosition(const Piece& piece) {
    // Get the base board position with proper offset
    Vector2 boardPos = BoardToScreen(piece.GetX(), piece.GetY());

    // Center the piece within its tile
    return (Vector2){
        boardPos.x + (TILE_SIZE - piece.GetTexture().width) / 2,
        boardPos.y + (TILE_SIZE - piece.GetTexture().height) / 2
    };
}

const Team& Game::GetWhiteTeam() const {
    return whiteTeam;
}

const Team& Game::GetBlackTeam() const {
    return blackTeam;
}

void Game::SelectPiece(int x, int y) {
    // Clear previous selection
    selectedPiece = nullptr;
    validMoves.clear();

    // Get piece at the clicked position
    const Piece* piece = GetPieceAt(x, y);
    if (piece && piece->IsWhite() == isWhiteTurn) {
        selectedPiece = const_cast<Piece*>(piece);
        validMoves = GetValidMoves(selectedPiece);
    }
}

bool Game::IsSquareUnderAttack(int x, int y, bool byWhite, Vector2 ignorePiecePos) const {
    const Team& attackingTeam = byWhite ? whiteTeam : blackTeam;
    
    // Check all pieces of the attacking team
    for (const auto& piece : attackingTeam.GetPieces()) {
        // Skip if this is the piece position we're ignoring (for capture validation)
        if (piece->GetX() == ignorePiecePos.x && piece->GetY() == ignorePiecePos.y) {
            continue;
        }
        
        // Get all possible moves for this piece
        auto moves = piece->GetValidMoves(*this);
        
        // Check if any move can reach the target square
        for (const auto& move : moves) {
            if (move.x == x && move.y == y) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Vector2> Game::GetValidMoves(Piece* piece) {
    if (!piece) return {};

    std::vector<Vector2> moves = piece->GetValidMoves(*this);
    std::vector<Vector2> legalMoves;

    // Find our king
    const Team& ourTeam = piece->IsWhite() ? whiteTeam : blackTeam;
    const Piece* ourKing = nullptr;
    for (const auto& p : ourTeam.GetPieces()) {
        if (p->GetType() == PieceType::KING) {
            ourKing = p.get();
            break;
        }
    }

    if (!ourKing) return moves; // Should never happen in a valid game

    // Store original position
    Vector2 originalPos = piece->GetPosition();

    // Test each move to see if it leaves our king in check
    for (const auto& move : moves) {
        // Store piece at target position (if any)
        Piece* capturedPiece = const_cast<Piece*>(GetPieceAt(move.x, move.y));
        bool wasCaptured = false;

        // Temporarily move the piece
        piece->SetPosition(move.x, move.y);

        // Remove captured piece from board temporarily
        if (capturedPiece) {
            wasCaptured = true;
            capturedPiece->SetPosition(-1, -1);  // Move it off the board
        }

        // Check if the move puts our king in check
        bool kingInCheck;
        if (piece->GetType() == PieceType::KING) {
            kingInCheck = IsSquareUnderAttack(move.x, move.y, !piece->IsWhite(), originalPos);
        } else {
            kingInCheck = IsSquareUnderAttack(ourKing->GetX(), ourKing->GetY(), !piece->IsWhite(), originalPos);
        }

        // Restore board state
        piece->SetPosition(originalPos.x, originalPos.y);
        if (wasCaptured) {
            capturedPiece->SetPosition(move.x, move.y);  // Restore captured piece
        }

        // If the move doesn't leave our king in check, it's legal
        if (!kingInCheck) {
            legalMoves.push_back(move);
        }
    }

    return legalMoves;
}

bool Game::IsCheckmate(bool isWhite) const {
    // Find the king
    const Team& team = isWhite ? whiteTeam : blackTeam;
    const Piece* king = nullptr;
    for (const auto& piece : team.GetPieces()) {
        if (piece->GetType() == PieceType::KING) {
            king = piece.get();
            break;
        }
    }
    if (!king) return false; // Should never happen in a valid game

    // Check if the king is in check
    if (!IsSquareUnderAttack(king->GetX(), king->GetY(), !isWhite)) {
        return false; // Not in check, so not checkmate
    }

    // Check if any piece can make a legal move
    for (const auto& piece : team.GetPieces()) {
        // Get all possible moves for this piece
        auto moves = piece->GetValidMoves(*this);
        
        // Test each move to see if it gets the king out of check
        for (const auto& move : moves) {
            // Store original position
            Vector2 originalPos = piece->GetPosition();
            
            // Temporarily move the piece
            const_cast<Piece*>(piece.get())->SetPosition(move.x, move.y);
            
            // Check if the king is still in check
            bool stillInCheck = IsSquareUnderAttack(
                king->GetX(), 
                king->GetY(), 
                !isWhite,
                originalPos // Ignore the piece we just moved
            );
            
            // Restore original position
            const_cast<Piece*>(piece.get())->SetPosition(originalPos.x, originalPos.y);
            
            if (!stillInCheck) {
                return false; // Found a move that gets the king out of check
            }
        }
    }
    
    return true; // No legal moves found that get the king out of check
}

const Piece* Game::GetPieceAt(int x, int y) const {
    // Check white team pieces
    for (const auto& piece : whiteTeam.GetPieces()) {
        if (piece->GetX() == x && piece->GetY() == y) {
            return piece.get();
        }
    }
    
    // Check black team pieces
    for (const auto& piece : blackTeam.GetPieces()) {
        if (piece->GetX() == x && piece->GetY() == y) {
            return piece.get();
        }
    }
    
    return nullptr;
}

void Game::ToggleBoardRotation() {
    boardRotated = !boardRotated;
}

void Game::DrawCheckmateMenu() {
    // Draw semi-transparent background
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 200});

    // Get window dimensions for centering
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();

    // Draw checkmate message
    const char* message = isWhiteTurn ? "Black wins by checkmate!" : "White wins by checkmate!";
    const char* playAgainText = "Play Again";
    const char* quitText = "Quit";

    int messageWidth = MeasureText(message, 40);
    int playAgainWidth = MeasureText(playAgainText, 30);
    int quitWidth = MeasureText(quitText, 30);

    // Draw checkmate message
    DrawText(message, 
        (windowWidth - messageWidth) / 2,
        windowHeight / 3,
        40,
        RAYWHITE
    );

    // Draw Play Again button
    int buttonWidth = playAgainWidth + 60;
    int buttonHeight = 40;
    int buttonX = (windowWidth - buttonWidth) / 2;
    int buttonY = windowHeight / 2;

    // Draw button background with hover effect
    Vector2 mousePos = GetMousePosition();
    Color buttonColor = RAYWHITE;
    if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
        mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
        buttonColor = LIGHTGRAY;
    }
    DrawRectangle(buttonX, buttonY, buttonWidth, buttonHeight, buttonColor);
    
    // Draw button text
    DrawText(playAgainText,
        buttonX + (buttonWidth - playAgainWidth) / 2,
        buttonY + (buttonHeight - 30) / 2,
        30,
        BLACK
    );

    // Draw Quit button
    buttonY += buttonHeight + 20;
    buttonWidth = quitWidth + 60;
    buttonX = (windowWidth - buttonWidth) / 2;

    // Draw button background with hover effect
    buttonColor = RAYWHITE;
    if (mousePos.x >= buttonX && mousePos.x <= buttonX + buttonWidth &&
        mousePos.y >= buttonY && mousePos.y <= buttonY + buttonHeight) {
        buttonColor = LIGHTGRAY;
    }
    DrawRectangle(buttonX, buttonY, buttonWidth, buttonHeight, buttonColor);
    
    // Draw button text
    DrawText(quitText,
        buttonX + (buttonWidth - quitWidth) / 2,
        buttonY + (buttonHeight - 30) / 2,
        30,
        BLACK
    );
}
