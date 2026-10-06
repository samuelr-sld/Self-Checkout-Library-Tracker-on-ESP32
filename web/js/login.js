import { signInWithEmailAndPassword } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-auth.js";

const form = document.getElementById('loginForm');
const button = document.getElementById('loginBtn');
const message = document.getElementById('loginMessage');

function showMessage(text, type) {
    message.textContent = text;
    message.className = 'status-message ' + type;
}

// Already signed in (saved session)? Skip the form.
window.authReady.then((user) => {
    if (user) window.location.replace('index.html');
});

form.addEventListener('submit', async (e) => {
    e.preventDefault();

    button.disabled = true;
    button.textContent = 'Signing in...';
    showMessage('', '');

    try {
        await signInWithEmailAndPassword(
            window.auth,
            document.getElementById('loginEmail').value.trim(),
            document.getElementById('loginPassword').value
        );
        window.location.replace('index.html');
    } catch (error) {
        console.error('Sign-in failed:', error.code);
        // Same message for every failure so the form doesn't reveal which emails exist
        showMessage('Sign-in failed. Check your email and password.', 'error');
        button.disabled = false;
        button.textContent = 'Sign in';
    }
});
