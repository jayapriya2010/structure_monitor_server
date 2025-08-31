const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const { MongoClient, ServerApiVersion } = require('mongodb');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

// MongoDB connection URI
const uri = process.env.MONGODB_URI;

const client = new MongoClient(uri, {
    serverApi: {
        version: ServerApiVersion.v1,
        strict: true,
        deprecationErrors: true,
    }
});

let sensorCollection;

// Connect to MongoDB and keep connection open
async function connectMongo() {
    try {
        await client.connect();
        await client.db("admin").command({ ping: 1 });
        console.log("Pinged your deployment. You successfully connected to MongoDB!");
        // Use 'project1' database and 'sensorData' collection
        sensorCollection = client.db("project1").collection("sensorData");
    } catch (error) {
        console.error("MongoDB connection error:", error);
    }
}
connectMongo();

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
app.post('/api/sensor-data', async (req, res) => {
    console.log('Received data:', req.body);

    // Accept all new fields, set missing fields to 0 or null as appropriate
    const {
        pitch,
        roll,
        rms,
        peak,
        crest_factor,
        f_dom,
        temp,
        sw18010p,
        fsr_adc,
        fsr_force_N,
        fsr_pressure_Pa
    } = req.body;

    const newData = {
        pitch: pitch !== undefined && pitch !== null ? Number(pitch) : 0,
        roll: roll !== undefined && roll !== null ? Number(roll) : 0,
        rms: rms !== undefined && rms !== null ? Number(rms) : 0,
        peak: peak !== undefined && peak !== null ? Number(peak) : 0,
        crest_factor: crest_factor !== undefined && crest_factor !== null ? Number(crest_factor) : 0,
        f_dom: f_dom !== undefined && f_dom !== null ? Number(f_dom) : 0,
        temp: temp !== undefined && temp !== null ? Number(temp) : 0,
        sw18010p: sw18010p !== undefined && sw18010p !== null ? Number(sw18010p) : 0,
        fsr_adc: fsr_adc !== undefined && fsr_adc !== null ? Number(fsr_adc) : 0,
        fsr_force_N: fsr_force_N !== undefined && fsr_force_N !== null ? Number(fsr_force_N) : 0,
        fsr_pressure_Pa: fsr_pressure_Pa !== undefined && fsr_pressure_Pa !== null ? Number(fsr_pressure_Pa) : 0,
        timestamp: getISTDateTime(),
        id: Date.now().toString()
    };

    try {
        // Store in MongoDB
        await sensorCollection.insertOne(newData);

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
app.get('/api/sensor-data', async (req, res) => {
    const limit = parseInt(req.query.limit) || 10;
    try {
        // Fetch latest data from MongoDB
        const data = await sensorCollection.find({})
            .sort({ timestamp: -1 })
            .limit(limit)
            .toArray();

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
app.get('/api/sensor-data/latest', async (req, res) => {
    try {
        // Fetch latest single entry from MongoDB
        const latest = await sensorCollection.find({})
            .sort({ timestamp: -1 })
            .limit(1)
            .toArray();

        if (latest.length > 0) {
            res.status(200).json({
                success: true,
                data: latest[0]
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
