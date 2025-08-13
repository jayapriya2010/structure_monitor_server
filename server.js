const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

// Local storage for sensor data
const localDataStore = {
    sensorData: []
};

// Function to get the current IST date and time
function getISTDateTime() {
    return new Date().toLocaleString('en-US', {
        timeZone: 'Asia/Kolkata',
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false
    }).replace(/(\d+)\/(\d+)\/(\d+)/, '$3-$1-$2');
}

// Middleware
app.use(cors({
    origin: '*',
    methods: ['GET', 'POST'],
    allowedHeaders: ['Content-Type'],
    credentials: true
}));
app.use(bodyParser.json());

// Root endpoint
app.get('/', (req, res) => {
    res.send('Welcome to Water Level and Temperature Monitoring System API');
});

// POST endpoint for sensor data
app.post('/api/sensor-data', (req, res) => {
    console.log('Received data:', req.body);

    const {
        inclination,
        vibration,
        pressure,
        force
    } = req.body;

    // Accept partial data, set missing fields to 0
    const newData = {
        inclination: inclination !== undefined && inclination !== null ? Number(inclination) : 0,
        vibration: vibration !== undefined && vibration !== null ? Number(vibration) : 0,
        pressure: pressure !== undefined && pressure !== null ? Number(pressure) : 0,
        force: force !== undefined && force !== null ? Number(force) : 0,
        timestamp: getISTDateTime(),
        id: Date.now().toString()
    };

    try {
        // Store in local memory
        localDataStore.sensorData.unshift(newData);
        if (localDataStore.sensorData.length > 100) {
            localDataStore.sensorData = localDataStore.sensorData.slice(0, 100);
        }

        res.status(200).json({
            success: true,
            message: 'Data stored successfully',
            latestData: newData
        });
    } catch (error) {
        console.error('Error:', error);
        res.status(500).json({
            success: false,
            message: error.message
        });
    }
});

// GET endpoint for sensor data
app.get('/api/sensor-data', (req, res) => {
    const limit = parseInt(req.query.limit) || 10;
    
    try {
        const data = localDataStore.sensorData.slice(0, limit);
        
        res.status(200).json({
            success: true,
            data: data
        });
    } catch (error) {
        console.error('Error fetching data:', error);
        res.status(500).json({
            success: false,
            message: 'Error retrieving data'
        });
    }
});

// GET endpoint for latest reading
app.get('/api/sensor-data/latest', (req, res) => {
    try {
        if (localDataStore.sensorData.length > 0) {
            res.status(200).json({
                success: true,
                data: localDataStore.sensorData[0]
            });
        } else {
            res.status(404).json({
                success: false,
                message: 'No data available'
            });
        }
    } catch (error) {
        console.error('Error:', error);
        res.status(500).json({
            success: false,
            message: 'Error retrieving data'
        });
    }
});

// Start server
app.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
});