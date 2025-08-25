import { useState } from 'react';
import { Form, Button } from 'react-bootstrap';

interface WiFiFormProps {
  ssid: string;
  onSaveSuccess: () => void;
  onSaveError: () => void;
}

const WiFiForm = ({ ssid, onSaveSuccess, onSaveError }: WiFiFormProps) => {
  const [password, setPassword] = useState('');
  const [isSubmitting, setIsSubmitting] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsSubmitting(true);

    try {
      const response = await fetch('/save_wifi', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ ssid, password }),
      });

      const data = await response.json();

      if (data.success) {
        onSaveSuccess();
        setPassword('');
      } else {
        onSaveError();
      }
    } catch (error) {
      console.error('Error saving WiFi credentials:', error);
      onSaveError();
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <Form onSubmit={handleSubmit} className="mt-3">
      <Form.Group className="mb-3">
        <Form.Label>Network</Form.Label>
        <Form.Control
          type="text"
          value={ssid}
          disabled
          className="bg-light"
        />
      </Form.Group>

      <Form.Group className="mb-3">
        <Form.Label>Password</Form.Label>
        <Form.Control
          type="password"
          placeholder="Enter network password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
          autoFocus
        />
      </Form.Group>

      <Button 
        variant="primary" 
        type="submit"
        disabled={isSubmitting}
        className="w-100"
      >
        {isSubmitting ? 'Saving...' : 'Save Credentials'}
      </Button>
    </Form>
  );
};

export default WiFiForm;
