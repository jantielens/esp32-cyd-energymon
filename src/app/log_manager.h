/*
 * Log Manager - Serial Logging with Formatting
 * 
 * Provides structured logging with:
 * - Nested blocks with automatic indentation
 * - Automatic timing for each block
 * - Printf-style formatting
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>

class LogManager : public Print {
public:
    LogManager();
    
    // Initialize with hardware serial
    void begin(unsigned long baud);
    
    // Print interface implementation (backward compatible)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    
    // Nested logging with automatic timing
    void logBegin(const char* module);
    void logLine(const char* message);
    void logLinef(const char* format, ...);
    void logEnd(const char* message = nullptr);
    
    // Convenience methods (begin + line + end)
    void logMessage(const char* module, const char* message);
    void logMessagef(const char* module, const char* format, ...);
    
    // Single-line logging with timing (no nesting)
    void logQuick(const char* module, const char* message);
    void logQuickf(const char* module, const char* format, ...);
    
private:
    // Nested block tracking
    static unsigned long startTimes[3];  // Start time for each nesting level
    static uint8_t nestLevel;            // Current nesting depth (0-2, 3+ = overflow)
    
    // Helper methods
    const char* indent();                // Get indentation string for current level
};

// Global instance (to replace Serial usage)
extern LogManager Logger;

#endif // LOG_MANAGER_H
