document.addEventListener('DOMContentLoaded', function() {
    const hasCardsList = !!document.getElementById('cardsList');
    const hasBarcodesList = !!document.getElementById('barcodesList');
    const hasCheckoutsList = !!document.getElementById('checkoutsList');
    const hasReturnsList = !!document.getElementById('returnsList');

    // Wait for Firebase to restore the saved session. Signed-out visitors go to the login page.
    // (This is a convenience; the real protection is firestore.rules.)
    if (!window.authReady) console.error('Firebase not initialized');
    (window.authReady || Promise.resolve(null)).then((user) => {
        if (!user) {
            window.location.replace('login.html');
            return;
        }
        document.documentElement.classList.add('authed');
        addSignOutBar(user);

        if (hasCardsList) loadCards();
        if (hasBarcodesList) loadBarcodes();
        if (hasCheckoutsList) loadCheckouts();
        if (hasReturnsList) loadReturns();
        startRealTimeSync(); // Start listening for real-time sync (dashboard, RFID, barcode)
        
        // Start RFID listener if on register-cards page
        if (document.getElementById('rfidStatus')) {
            startRFIDListener();
        }
        
        // Start barcode scanner for register-books page
        if (document.getElementById('barcode') && document.getElementById('barcodeForm')) {
            setupBarcodeScannerForRegistration();
        }
    });

    // Shows who is signed in, with a sign-out button, at the bottom of the page header
    function addSignOutBar(user) {
        const header = document.querySelector('.admin-header');
        if (!header) return;

        const bar = document.createElement('div');
        bar.className = 'auth-bar';

        const who = document.createElement('span');
        who.textContent = user.email || 'Signed in';

        const signOutBtn = document.createElement('button');
        signOutBtn.type = 'button';
        signOutBtn.className = 'tab-btn';
        signOutBtn.textContent = 'Sign out';
        signOutBtn.addEventListener('click', async () => {
            const { signOut } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-auth.js');
            await signOut(window.auth);
            window.location.replace('login.html');
        });

        bar.append(who, signOutBtn);
        header.appendChild(bar);
    }

    // Card Form Handler
    const cardForm = document.getElementById('cardForm');
    if (cardForm) {
        const cardSubmitBtn = document.getElementById('cardSubmitBtn');
        const cardStatusMessage = document.getElementById('cardStatusMessage');

        cardForm.addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const cardData = {
                cardId: document.getElementById('cardId').value.trim(),
                name: document.getElementById('cardName').value.trim(),
                email: document.getElementById('cardEmail').value.trim(),
                phone: document.getElementById('cardPhone').value.trim(),
                status: document.getElementById('cardStatus').value,
                timestamp: new Date(),
                type: 'card'
            };
            
            // Validation
            if (!cardData.cardId || !cardData.name) {
                showStatus(cardStatusMessage, 'Please fill in required fields (Card ID and Name).', 'error');
                return;
            }
            
            // Disable submit button
            cardSubmitBtn.disabled = true;
            cardSubmitBtn.textContent = 'Registering...';
            showStatus(cardStatusMessage, '', '');
            
            try {
                const { collection, addDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
                
                await addDoc(collection(window.db, 'cards'), cardData);
                
                // Send welcome email if email is provided
                if (cardData.email) {
                    await sendWelcomeEmail(cardData);
                }
                
                console.log('Card registered successfully');
                showStatus(cardStatusMessage, 'Card registered successfully!', 'success');
                cardForm.reset();
                loadCards(); // Refresh the list
                
            } catch (error) {
                console.error('Error registering card: ', error);
                showStatus(cardStatusMessage, 'Error registering card. Please try again.', 'error');
            } finally {
                cardSubmitBtn.disabled = false;
                cardSubmitBtn.textContent = 'Register Card';
            }
        });
    }

    // Barcode Form Handler
    const barcodeForm = document.getElementById('barcodeForm');
    if (barcodeForm) {
        const barcodeSubmitBtn = document.getElementById('barcodeSubmitBtn');
        const barcodeStatusMessage = document.getElementById('barcodeStatusMessage');

        barcodeForm.addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const barcodeData = {
                barcode: document.getElementById('barcode').value.trim(),
                title: document.getElementById('itemTitle').value.trim(),
                author: document.getElementById('itemAuthor').value.trim(),
                itemType: document.getElementById('itemType').value,
                status: document.getElementById('itemStatus').value,
                timestamp: new Date(),
                type: 'barcode'
            };
            
            // Validation
            if (!barcodeData.barcode || !barcodeData.title) {
                showStatus(barcodeStatusMessage, 'Please fill in required fields (Barcode and Title).', 'error');
                return;
            }
            
            // Disable submit button
            barcodeSubmitBtn.disabled = true;
            barcodeSubmitBtn.textContent = 'Registering...';
            showStatus(barcodeStatusMessage, '', '');
            
            try {
                const { collection, addDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
                
                await addDoc(collection(window.db, 'barcodes'), barcodeData);
                
                console.log('Barcode registered successfully');
                showStatus(barcodeStatusMessage, 'Barcode registered successfully!', 'success');
                barcodeForm.reset();
                loadBarcodes(); // Refresh the list
                
            } catch (error) {
                console.error('Error registering barcode: ', error);
                showStatus(barcodeStatusMessage, 'Error registering barcode. Please try again.', 'error');
            } finally {
                barcodeSubmitBtn.disabled = false;
                barcodeSubmitBtn.textContent = 'Register Barcode';
            }
        });
    }

    // Tab Switching (only for in-page tab buttons with `data-tab`)
    const tabBtns = document.querySelectorAll('.tab-btn[data-tab]');
    if (tabBtns.length) {
        tabBtns.forEach(btn => {
            btn.addEventListener('click', function(e) {
                const targetTab = this.getAttribute('data-tab');
                if (!targetTab) return; // nothing to do for header/nav links

                // prevent default for in-page tab behavior (if anchor/button)
                if (e && typeof e.preventDefault === 'function') e.preventDefault();

                // Only consider tab buttons that control in-page content
                document.querySelectorAll('.tab-btn[data-tab]').forEach(b => b.classList.remove('active'));
                document.querySelectorAll('.tab-content').forEach(content => {
                    content.classList.remove('active');
                });

                this.classList.add('active');
                const targetEl = document.getElementById(targetTab + 'Tab');
                if (targetEl) targetEl.classList.add('active');
            });
        });
    }

    // Load and Display Functions
    async function loadCards() {
        try {
            const { collection, getDocs } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const querySnapshot = await getDocs(collection(window.db, 'cards'));
            const cardsList = document.getElementById('cardsList');
            
            if (querySnapshot.empty) {
                cardsList.innerHTML = '<p class="empty-text">No cards registered yet.</p>';
                return;
            }
            
            cardsList.innerHTML = '';
            querySnapshot.forEach((doc) => {
                const data = doc.data();
                const cardCard = createCardElement(data, doc.id);
                cardsList.appendChild(cardCard);
            });
        } catch (error) {
            console.error('Error loading cards: ', error);
            document.getElementById('cardsList').innerHTML = '<p class="empty-text">Error loading cards.</p>';
        }
    }

    async function loadBarcodes() {
        try {
            const { collection, getDocs } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const querySnapshot = await getDocs(collection(window.db, 'barcodes'));
            const barcodesList = document.getElementById('barcodesList');
            
            if (querySnapshot.empty) {
                barcodesList.innerHTML = '<p class="empty-text">No barcodes registered yet.</p>';
                return;
            }
            
            barcodesList.innerHTML = '';
            querySnapshot.forEach((doc) => {
                const data = doc.data();
                const barcodeCard = createBarcodeElement(data, doc.id);
                barcodesList.appendChild(barcodeCard);
            });
        } catch (error) {
            console.error('Error loading barcodes: ', error);
            document.getElementById('barcodesList').innerHTML = '<p class="empty-text">Error loading barcodes.</p>';
        }
    }

    function createCardElement(data, docId) {
        const card = document.createElement('div');
        card.className = 'item-card';
        card.innerHTML = `
            <h3>${escapeHtml(data.name || 'N/A')}</h3>
            <p><strong>Card ID:</strong> <span class="item-id">${escapeHtml(data.cardId)}</span></p>
            ${data.email ? `<p><strong>Email:</strong> ${escapeHtml(data.email)}</p>` : ''}
            ${data.phone ? `<p><strong>Phone:</strong> ${escapeHtml(data.phone)}</p>` : ''}
            <span class="status-badge ${data.status || 'active'}">${data.status || 'active'}</span>
            <p style="margin-top: 10px; font-size: 0.8rem; color: #999;">
                Registered: ${formatDate(data.timestamp)}
            </p>
        `;
        return card;
    }

    function createBarcodeElement(data, docId) {
        const card = document.createElement('div');
        card.className = 'item-card';
        card.innerHTML = `
            <h3>${escapeHtml(data.title || 'N/A')}</h3>
            <p><strong>Barcode:</strong> <span class="item-id">${escapeHtml(data.barcode)}</span></p>
            ${data.author ? `<p><strong>Author:</strong> ${escapeHtml(data.author)}</p>` : ''}
            <p><strong>Type:</strong> ${escapeHtml(data.itemType || 'other')}</p>
            <span class="status-badge ${data.status || 'available'}">${data.status || 'available'}</span>
            <p style="margin-top: 10px; font-size: 0.8rem; color: #999;">
                Registered: ${formatDate(data.timestamp)}
            </p>
        `;
        return card;
    }

    function showStatus(element, message, type) {
        element.textContent = message;
        element.className = 'status-message ' + type;
        
        if (type === 'success') {
            setTimeout(() => {
                element.textContent = '';
                element.className = 'status-message';
            }, 5000);
        }
    }

    function escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    function formatDate(timestamp) {
        if (!timestamp) return 'N/A';
        const date = timestamp.toDate ? timestamp.toDate() : new Date(timestamp);
        return date.toLocaleDateString() + ' ' + date.toLocaleTimeString();
    }

    // Load Checkouts
    async function loadCheckouts() {
        try {
            const { collection, getDocs, orderBy, query } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const q = query(collection(window.db, 'checkouts'), orderBy('timestamp', 'desc'));
            const querySnapshot = await getDocs(q);
            const checkoutsList = document.getElementById('checkoutsList');
            
            if (querySnapshot.empty) {
                checkoutsList.innerHTML = '<p class="empty-text">No checkouts yet.</p>';
                return;
            }
            
            checkoutsList.innerHTML = '';
            querySnapshot.forEach((doc) => {
                const data = doc.data();
                const checkoutCard = createCheckoutElement(data, doc.id);
                checkoutsList.appendChild(checkoutCard);
            });
        } catch (error) {
            console.error('Error loading checkouts: ', error);
            document.getElementById('checkoutsList').innerHTML = '<p class="empty-text">Error loading checkouts.</p>';
        }
    }

    function createCheckoutElement(data, docId) {
        const card = document.createElement('div');
        card.className = 'item-card';
        card.innerHTML = `
            <h3>Checkout Transaction</h3>
            <p><strong>User:</strong> ${escapeHtml(data.userName || 'Unknown')}</p>
            ${data.studentId ? `<p><strong>Student ID:</strong> ${escapeHtml(data.studentId)}</p>` : ''}
            <p><strong>Book:</strong> ${escapeHtml(data.bookName || data.barcode || 'Unknown')}</p>
            ${data.bookAuthor ? `<p><strong>Author:</strong> ${escapeHtml(data.bookAuthor)}</p>` : ''}
            <p><strong>Card ID:</strong> <span class="item-id">${escapeHtml(data.cardId || 'N/A')}</span></p>
            <p><strong>Barcode:</strong> <span class="item-id">${escapeHtml(data.barcode || 'N/A')}</span></p>
            <p style="margin-top: 10px; font-size: 0.8rem; color: #999;">
                ${formatDate(data.timestamp)}
            </p>
        `;
        return card;
    }

    // Load Returns
    async function loadReturns() {
        try {
            const { collection, getDocs, orderBy, query } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const q = query(collection(window.db, 'returns'), orderBy('timestamp', 'desc'));
            const querySnapshot = await getDocs(q);
            const returnsList = document.getElementById('returnsList');
            
            if (querySnapshot.empty) {
                returnsList.innerHTML = '<p class="empty-text">No returns yet.</p>';
                return;
            }
            
            returnsList.innerHTML = '';
            querySnapshot.forEach((doc) => {
                const data = doc.data();
                const returnCard = createReturnElement(data, doc.id);
                returnsList.appendChild(returnCard);
            });
        } catch (error) {
            console.error('Error loading returns: ', error);
            document.getElementById('returnsList').innerHTML = '<p class="empty-text">Error loading returns.</p>';
        }
    }

    function createReturnElement(data, docId) {
        const card = document.createElement('div');
        card.className = 'item-card';
        card.innerHTML = `
            <h3>Return Transaction</h3>
            <p><strong>User:</strong> ${escapeHtml(data.userName || 'Unknown')}</p>
            ${data.studentId ? `<p><strong>Student ID:</strong> ${escapeHtml(data.studentId)}</p>` : ''}
            <p><strong>Book:</strong> ${escapeHtml(data.bookName || data.barcode || 'Unknown')}</p>
            ${data.bookAuthor ? `<p><strong>Author:</strong> ${escapeHtml(data.bookAuthor)}</p>` : ''}
            <p><strong>Card ID:</strong> <span class="item-id">${escapeHtml(data.cardId || 'N/A')}</span></p>
            <p><strong>Barcode:</strong> <span class="item-id">${escapeHtml(data.barcode || 'N/A')}</span></p>
            <p style="margin-top: 10px; font-size: 0.8rem; color: #999;">
                ${formatDate(data.timestamp)}
            </p>
        `;
        return card;
    }

    // Barcode input handler (USB scanner or manual input)
    const manualBarcodeInput = document.getElementById('manualBarcode');
    const sendBarcodeBtn = document.getElementById('sendBarcodeBtn');
    let barcodeInputBuffer = '';
    let barcodeInputTimeout = null;
    
    if (manualBarcodeInput && sendBarcodeBtn) {
        manualBarcodeInput.focus();
        
        manualBarcodeInput.addEventListener('input', function() {
            clearTimeout(barcodeInputTimeout);
            barcodeInputBuffer = manualBarcodeInput.value;
            barcodeInputTimeout = setTimeout(() => {
                if (barcodeInputBuffer.length > 0) {
                    sendBarcodeToESP32();
                }
            }, 200);
        });
        
        manualBarcodeInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                clearTimeout(barcodeInputTimeout);
                sendBarcodeToESP32();
            }
        });
        
        sendBarcodeBtn.addEventListener('click', function() {
            clearTimeout(barcodeInputTimeout);
            sendBarcodeToESP32();
        });
        
        document.addEventListener('click', function() {
            if (document.activeElement !== manualBarcodeInput) {
                manualBarcodeInput.focus();
            }
        });
    }
    
    // Real-time sync for barcode from website to ESP32
    async function sendBarcodeToESP32() {
        const barcode = manualBarcodeInput ? manualBarcodeInput.value.trim() : '';
        if (!barcode) {
            alert('Please enter a barcode');
            return;
        }
        
        try {
            const { collection, addDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            
            // Create sync document with barcode from website
            const syncData = {
                barcode: barcode,
                type: 'barcode_from_website',
                timestamp: new Date(),
                processed: false,
                esp32Received: false
            };
            
            const docRef = await addDoc(collection(window.db, 'sync'), syncData);
            console.log('Barcode sent to ESP32:', barcode);
            
            updateSyncStatus(barcode);
            
            if (manualBarcodeInput) {
                manualBarcodeInput.value = '';
                manualBarcodeInput.focus();
            }
            
            // Look up book info
            const bookInfo = await lookupBook(barcode);
            
            // Send book info response to ESP32
            if (bookInfo) {
                await sendBookInfoResponse(docRef.id, bookInfo);
            } else {
                await sendBookInfoResponse(docRef.id, { 
                    bookName: 'Book not found', 
                    bookAuthor: 'Please register this barcode' 
                }, true); // Pass true to indicate not found
            }
            
        } catch (error) {
            console.error('Error sending barcode to ESP32:', error);
            alert('Error sending barcode. Please try again.');
        }
    }
    
    // Real-time sync - listen for ESP32 responses (if needed)
    async function startRealTimeSync() {
        // The website sends barcodes to ESP32, not the other way around
        // This function can be used for future bidirectional sync if needed
        console.log('Real-time sync ready - waiting for barcode input');
    }

    function updateSyncStatus(barcode) {
        const syncStatus = document.getElementById('syncStatus');
        const bookInfoDisplay = document.getElementById('bookInfoDisplay');
        
        syncStatus.innerHTML = `
            <p style="color: #667eea; font-weight: 600;">📖 Barcode Received:</p>
            <p style="font-family: monospace; font-size: 1.1rem; color: #333;">${escapeHtml(barcode)}</p>
            <p style="color: #999; font-size: 0.9rem; margin-top: 10px;">Looking up book information...</p>
        `;
        bookInfoDisplay.style.display = 'none';
    }

    async function lookupBook(barcode) {
        try {
            const { collection, getDocs, query, where } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const q = query(collection(window.db, 'barcodes'), where('barcode', '==', barcode));
            const querySnapshot = await getDocs(q);
            
            if (!querySnapshot.empty) {
                const doc = querySnapshot.docs[0];
                const data = doc.data();
                return {
                    bookName: data.title || 'Unknown',
                    bookAuthor: data.author || ''
                };
            }
            return null;
        } catch (error) {
            console.error('Error looking up book:', error);
            return null;
        }
    }

    async function sendBookInfoResponse(syncDocId, bookInfo, notFound = false) {
        try {
            const { doc, updateDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            const syncDoc = doc(window.db, 'sync', syncDocId);
            
            // Update sync document with book info and mark as processed
            await updateDoc(syncDoc, {
                bookName: bookInfo.bookName,
                bookAuthor: bookInfo.bookAuthor || '',
                processed: true
            });
            
            console.log('Book info sent to ESP32:', bookInfo);
            
            // Update UI on website
            const bookInfoDisplay = document.getElementById('bookInfoDisplay');
            const notFoundActions = document.getElementById('bookNotFoundActions');
            
            if (bookInfoDisplay) {
                document.getElementById('foundBookTitle').textContent = bookInfo.bookName;
                document.getElementById('foundBookAuthor').textContent = bookInfo.bookAuthor || 'N/A';
                bookInfoDisplay.style.display = 'block';
                
                // Show register button if book not found
                if (notFound && notFoundActions) {
                    notFoundActions.style.display = 'block';
                    bookInfoDisplay.style.background = '#fff3cd';
                } else if (notFoundActions) {
                    notFoundActions.style.display = 'none';
                    bookInfoDisplay.style.background = '#e8f5e9';
                }
            }
            
            // Refresh checkouts and returns after a moment
            setTimeout(() => {
                if (typeof loadCheckouts === 'function') {
                    loadCheckouts();
                }
                if (typeof loadReturns === 'function') {
                    loadReturns();
                }
            }, 1000);
            
        } catch (error) {
            console.error('Error sending book info response:', error);
        }
    }
    
    // RFID Listener for register-cards page
    async function startRFIDListener() {
        try {
            const { collection, onSnapshot, query, where } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
            
            // Listen for RFID from ESP32
            const syncRef = collection(window.db, 'sync');
            const q = query(syncRef, where('type', '==', 'rfid_for_registration'), where('processed', '==', false));
            
            onSnapshot(q, async (snapshot) => {
                snapshot.docChanges().forEach(async (change) => {
                    if (change.type === 'added') {
                        const data = change.doc.data();
                        const docId = change.doc.id;
                        const rfid = data.rfid || data.cardId;
                        
                        if (rfid) {
                            console.log('RFID received from ESP32:', rfid);
                            
                            // Auto-fill the cardId field
                            const cardIdInput = document.getElementById('cardId');
                            if (cardIdInput) {
                                cardIdInput.value = rfid;
                                
                                // Update status
                                const rfidStatus = document.getElementById('rfidStatus');
                                if (rfidStatus) {
                                    rfidStatus.innerHTML = `
                                        <p style="color: #667eea; font-weight: 600;">✅ RFID Card Detected!</p>
                                        <p style="font-family: monospace; font-size: 1.1rem; color: #333; margin-top: 10px;">${escapeHtml(rfid)}</p>
                                    `;
                                }
                                
                                // Mark as processed
                                const { doc, updateDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
                                const syncDoc = doc(window.db, 'sync', docId);
                                await updateDoc(syncDoc, { processed: true });
                            }
                        }
                    }
                });
            });
            
            console.log('RFID listener started for card registration');
        } catch (error) {
            console.error('Error starting RFID listener:', error);
        }
    }
    
    // Barcode Scanner Setup for register-books page - direct input to form field
    function setupBarcodeScannerForRegistration() {
        const barcodeInput = document.getElementById('barcode');
        if (!barcodeInput) return;
        
        // Auto-focus the barcode input field for easy scanning
        barcodeInput.focus();
        
        // Barcode scanners send characters rapidly, so we just let it fill the field
        // The form will handle the submission normally
    }
    
    // Send Welcome Email via Firebase Trigger Email Extension
    async function sendWelcomeEmail(cardData) {
    try {
        const { collection, addDoc } = await import('https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js');
        
        const mailData = {
            to: [cardData.email],   // ✅ MUST BE ARRAY
            message: {
                subject: 'Welcome! Your Library Card Account Created Successfully',
                text: `Welcome to the Library System!

Dear ${cardData.name},

Your library card account has been successfully created.

Name: ${cardData.name}
Card ID: ${cardData.cardId}
Status: ${cardData.status || 'Active'}

You can now use your library card to check out books.

Library Management System
                `,
                html: `
                    <h2>Welcome to the Library System!</h2>
                    <p>Dear <strong>${escapeHtml(cardData.name)}</strong>,</p>
                    <p>Congratulations! Your library card account has been successfully created.</p>
                    <p><strong>Account Details:</strong></p>
                    <ul>
                        <li><strong>Name:</strong> ${escapeHtml(cardData.name)}</li>
                        <li><strong>Card ID:</strong> ${escapeHtml(cardData.cardId)}</li>
                        ${cardData.phone ? `<li><strong>Phone:</strong> ${escapeHtml(cardData.phone)}</li>` : ''}
                        <li><strong>Status:</strong> ${escapeHtml(cardData.status || 'Active')}</li>
                    </ul>
                    <p>You can now use your library card to check out books and access library resources.</p>
                    <p>If you have any questions, please contact us.</p>
                    <p>Best regards,<br>Library Management System</p>
                `
            }
        };
        
        await addDoc(collection(window.db, 'mail'), mailData);
        console.log('Welcome email queued for:', cardData.email);
    } catch (error) {
        console.error('Error queueing welcome email:', error);
    }
}
});