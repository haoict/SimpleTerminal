// Application state
let currentFont = 0;
let currentChar = 65; // Start with 'A'
let currentMatrix = [];
let undoStack = [];
let redoStack = [];
const MAX_HISTORY = 50;
let originalFontData = [];

// DOM elements
const fontSelect = document.getElementById('font-select');
const charSelect = document.getElementById('char-select');
const asciiInput = document.getElementById('ascii-input');
const matrixContainer = document.getElementById('matrix-container');
const hexOutput = document.getElementById('hex-output');
const characterGrid = document.getElementById('character-grid');

// Initialize
function init() {
    // Store original font data (deep copy)
    originalFontData = FONTS.map(font => ({
        ...font,
        data: font.data ? [...font.data] : []
    }));
    
    populateCharacterSelect();
    populateCharacterGrid();
    setupEventListeners();
    loadCharacter();
}

// Populate character select dropdown
function populateCharacterSelect() {
    charSelect.innerHTML = '';
    for (let i = 0; i < 128; i++) {
        const option = document.createElement('option');
        option.value = i;
        
        if (i < 32) {
            option.textContent = `(Control)`;
        } else if (i === 32) {
            option.textContent = `(Space)`;
        } else if (i === 127) {
            option.textContent = `(DEL)`;
        } else {
            option.textContent = `${String.fromCharCode(i)}`;
        }
        
        if (i === currentChar) {
            option.selected = true;
        }
        
        charSelect.appendChild(option);
    }
}

// Populate character grid with buttons
function populateCharacterGrid() {
    characterGrid.innerHTML = '';
    
    // Helper function to create character button
    function createCharButton(charCode) {
        const btn = document.createElement('button');
        btn.className = 'char-btn';
        btn.dataset.charCode = charCode;
        
        if (charCode < 32) {
            btn.textContent = '␀';
            btn.title = `${charCode}: (Control)`;
            btn.classList.add('control-char');
        } else if (charCode === 32) {
            btn.textContent = '␣';
            btn.title = `${charCode}: (Space)`;
        } else if (charCode === 127) {
            btn.textContent = '⌫';
            btn.title = `${charCode}: (DEL)`;
            btn.classList.add('control-char');
        } else {
            btn.textContent = String.fromCharCode(charCode);
            btn.title = `${charCode}: '${String.fromCharCode(charCode)}'`;
        }
        
        if (charCode === currentChar) {
            btn.classList.add('active');
        }
        
        btn.addEventListener('click', () => {
            currentChar = charCode;
            charSelect.value = charCode;
            asciiInput.value = charCode;
            updateCharacterGridSelection();
            loadCharacter();
        });
        
        return btn;
    }
    
    // Helper to add line break
    function addBreak() {
        const br = document.createElement('div');
        br.className = 'char-grid-break';
        characterGrid.appendChild(br);
    }
    
    // Control Characters (0-31, 127)
    for (let i = 0; i < 32; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    addBreak();
    // Numbers & Symbols (32-64)
    for (let i = 32; i <= 64; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    addBreak();
    // Uppercase (65-90)
    for (let i = 65; i <= 90; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    addBreak();
    // Lowercase (97-122)
    for (let i = 97; i <= 122; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    addBreak();
    // Special Characters (91-96, 123-126)
    for (let i = 91; i <= 96; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    for (let i = 123; i <= 126; i++) {
        characterGrid.appendChild(createCharButton(i));
    }
    characterGrid.appendChild(createCharButton(127));
}

// Update character grid selection
function updateCharacterGridSelection() {
    document.querySelectorAll('.char-btn').forEach(btn => {
        btn.classList.remove('active');
        if (parseInt(btn.dataset.charCode) === currentChar) {
            btn.classList.add('active');
        }
    });
}

// Setup event listeners
function setupEventListeners() {
    fontSelect.addEventListener('change', (e) => {
        currentFont = parseInt(e.target.value);
        loadCharacter();
    });
    
    charSelect.addEventListener('change', (e) => {
        currentChar = parseInt(e.target.value);
        asciiInput.value = currentChar;
        updateCharacterGridSelection();
        loadCharacter();
    });
    
    asciiInput.addEventListener('input', (e) => {
        let value = parseInt(e.target.value);
        if (value >= 0 && value <= 127) {
            currentChar = value;
            charSelect.value = currentChar;
            updateCharacterGridSelection();
            loadCharacter();
        }
    });
    
    document.getElementById('undo-btn').addEventListener('click', undo);
    document.getElementById('redo-btn').addEventListener('click', redo);
    document.getElementById('reset-btn').addEventListener('click', resetMatrix);
    document.getElementById('copy-hex-btn').addEventListener('click', copyHexData);
    document.getElementById('export-c-btn').addEventListener('click', exportAsC);
    
    // Toggle character grid
    document.getElementById('toggle-char-grid').addEventListener('click', function() {
        const grid = document.getElementById('character-grid');
        const btn = this;
        
        if (grid.classList.contains('collapsed')) {
            grid.classList.remove('collapsed');
            btn.textContent = '▼ Hide All Characters';
        } else {
            grid.classList.add('collapsed');
            btn.textContent = '▶ Show All Characters';
        }
    });
}

// Load character data into matrix
function loadCharacter() {
    const font = FONTS[currentFont];
    const startIdx = currentChar * font.bytesPerChar;
    const charBytes = font.data.slice(startIdx, startIdx + font.bytesPerChar);
    
    // Convert bytes to matrix
    currentMatrix = [];
    for (let row = 0; row < font.height; row++) {
        const rowData = [];
        const byte = charBytes[row] || 0;
        
        for (let col = 0; col < font.width; col++) {
            const bitPos = 7 - col; // MSB first
            const pixel = (byte >> bitPos) & 1;
            rowData.push(pixel);
        }
        
        currentMatrix.push(rowData);
    }
    
    // Clear undo/redo history when loading a new character
    undoStack = [];
    redoStack = [];
    
    renderMatrix();
    updateDataDisplay();
}

// Render the editable matrix
let isDrawing = false;
let drawState = 0;

function renderMatrix() {
    const font = FONTS[currentFont];
    matrixContainer.innerHTML = '';
    matrixContainer.style.gridTemplateColumns = `repeat(${font.width}, 30px)`;
    
    for (let row = 0; row < font.height; row++) {
        for (let col = 0; col < font.width; col++) {
            const pixel = document.createElement('div');
            pixel.className = `pixel ${currentMatrix[row][col] ? 'on' : 'off'}`;
            pixel.dataset.row = row;
            pixel.dataset.col = col;
            
            // Mouse down to start drawing
            pixel.addEventListener('mousedown', (e) => {
                e.preventDefault();
                isDrawing = true;
                drawState = currentMatrix[row][col] ? 0 : 1;
                saveStateForUndo();
                togglePixel(row, col, drawState);
            });
            
            // Mouse enter while dragging
            pixel.addEventListener('mouseenter', (e) => {
                if (isDrawing && e.buttons === 1) {
                    togglePixel(row, col, drawState);
                }
            });
            
            matrixContainer.appendChild(pixel);
        }
    }
    
    // Stop drawing when mouse is released anywhere
    document.addEventListener('mouseup', stopDrag);
}

function stopDrag() {
    isDrawing = false;
}

// Toggle pixel state
function togglePixel(row, col, state) {
    if (state !== undefined) {
        currentMatrix[row][col] = state;
    } else {
        currentMatrix[row][col] = currentMatrix[row][col] ? 0 : 1;
    }
    
    saveCharacter();
    renderMatrix();
    updateDataDisplay();
}

// Save current matrix back to font data
function saveCharacter() {
    const font = FONTS[currentFont];
    const startIdx = currentChar * font.bytesPerChar;
    
    for (let row = 0; row < font.height; row++) {
        let byte = 0;
        for (let col = 0; col < font.width; col++) {
            const bitPos = 7 - col; // MSB first
            if (currentMatrix[row][col]) {
                byte |= (1 << bitPos);
            }
        }
        font.data[startIdx + row] = byte;
    }
}

// Update data display
function updateDataDisplay() {
    const font = FONTS[currentFont];
    const startIdx = currentChar * font.bytesPerChar;
    const charBytes = font.data.slice(startIdx, startIdx + font.bytesPerChar);
    
    // Hex output
    const hexLines = [];
    for (let i = 0; i < charBytes.length; i++) {
        const byte = charBytes[i];
        const hex = '0x' + byte.toString(16).padStart(2, '0');
        const binary = byte.toString(2).padStart(8, '0');
        const visual = binary.replace(/0/g, ' ').replace(/1/g, '*');
        hexLines.push(`${hex}  // ${binary} -- ${visual}`);
    }
    hexOutput.textContent = hexLines.join('\n');
}

// History management
function saveStateForUndo() {
    // Deep copy the current matrix state
    const stateCopy = currentMatrix.map(row => [...row]);
    undoStack.push(stateCopy);
    
    // Limit history size
    if (undoStack.length > MAX_HISTORY) {
        undoStack.shift();
    }
    
    // Clear redo stack when new action is performed
    redoStack = [];
}

function undo() {
    if (undoStack.length === 0) return;
    
    // Save current state to redo stack
    const currentState = currentMatrix.map(row => [...row]);
    redoStack.push(currentState);
    
    // Restore previous state
    currentMatrix = undoStack.pop();
    
    saveCharacter();
    renderMatrix();
    updateDataDisplay();
}

function redo() {
    if (redoStack.length === 0) return;
    
    // Save current state to undo stack
    const currentState = currentMatrix.map(row => [...row]);
    undoStack.push(currentState);
    
    // Restore next state
    currentMatrix = redoStack.pop();
    
    saveCharacter();
    renderMatrix();
    updateDataDisplay();
}

function resetMatrix() {
    saveStateForUndo();
    
    // Restore original character data
    const originalFont = originalFontData[currentFont];
    const startIdx = currentChar * originalFont.bytesPerChar;
    const originalBytes = originalFont.data.slice(startIdx, startIdx + originalFont.bytesPerChar);
    
    // Convert original bytes back to matrix
    currentMatrix = [];
    for (let row = 0; row < originalFont.height; row++) {
        const rowData = [];
        const byte = originalBytes[row] || 0;
        
        for (let col = 0; col < originalFont.width; col++) {
            const bitPos = 7 - col; // MSB first
            const pixel = (byte >> bitPos) & 1;
            rowData.push(pixel);
        }
        
        currentMatrix.push(rowData);
    }
    
    saveCharacter();
    renderMatrix();
    updateDataDisplay();
}

// Copy hex data to clipboard
function copyHexData() {
    const font = FONTS[currentFont];
    const startIdx = currentChar * font.bytesPerChar;
    const charBytes = font.data.slice(startIdx, startIdx + font.bytesPerChar).map(b => `0x${b.toString(16).padStart(2, '0')}`);
    const text = charBytes.join(', ') + ',';
    navigator.clipboard.writeText(text).then(() => {
        const btn = document.getElementById('copy-hex-btn');
        const originalText = btn.textContent;
        btn.textContent = 'Copied!';
        setTimeout(() => {
            btn.textContent = originalText;
        }, 2000);
    });
}

// Export as C array
function exportAsC() {
    const font = FONTS[currentFont];
    let output = `// ${font.name}: ${font.width}x${font.height} pixels per character\n`;
    output += `static const unsigned char embedded_font${currentFont + 1}[] = {\n`;
    
    for (let charIdx = 0; charIdx < 128; charIdx++) {
        const startIdx = charIdx * font.bytesPerChar;
        const charBytes = font.data.slice(startIdx, startIdx + font.bytesPerChar);
        
        const hexValues = charBytes.map(b => '0x' + b.toString(16).padStart(2, '0')).join(', ');
        
        let charLabel = '';
        if (charIdx < 32) {
            charLabel = `${charIdx}: (Control)`;
        } else if (charIdx === 32) {
            charLabel = `${charIdx}: ' ' (space)`;
        } else if (charIdx === 127) {
            charLabel = `${charIdx}: DEL`;
        } else {
            charLabel = `${charIdx}: '${String.fromCharCode(charIdx)}'`;
        }
        
        output += `    ${hexValues},  // ${charLabel}\n`;
    }
    
    output += '};\n';
    
    downloadFile(`embedded_font${currentFont + 1}.c`, output);
}

// Download helper
function downloadFile(filename, content) {
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

// Start the app
init();
