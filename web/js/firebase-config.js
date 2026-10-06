// Import the functions you need from the SDKs you need
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-app.js";
import { getAnalytics } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-analytics.js";
import { getFirestore } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-firestore.js";
import { getAuth, onAuthStateChanged } from "https://www.gstatic.com/firebasejs/12.8.0/firebase-auth.js";

// Your web app's Firebase configuration
// (These values identify your project; they are not secrets. Access is enforced by
// firestore.rules, and you should restrict the API key in Google Cloud Console.)
const firebaseConfig = {
  apiKey: "AIzaSyCG_dtaIXO9TVNZJ45f3G7u_3KEKHMAoxc",
  authDomain: "self-checkout-library-system.firebaseapp.com",
  projectId: "self-checkout-library-system",
  storageBucket: "self-checkout-library-system.firebasestorage.app",
  messagingSenderId: "785948587406",
  appId: "1:785948587406:web:52600ce433c42d352320df",
  measurementId: "G-5CR5TE3K91"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);
const db = getFirestore(app);
const auth = getAuth(app);

// Make db and auth available globally for app.js and login.js
window.db = db;
window.auth = auth;

// Resolves with the signed-in user (or null) once Firebase has restored the saved session.
// Pages other than login.html send signed-out visitors to login.html.
window.authReady = new Promise((resolve) => {
  const unsubscribe = onAuthStateChanged(auth, (user) => {
    unsubscribe();
    resolve(user);
  });
});
